/* Simple HTTP Server Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_system.h>
#include <nvs_flash.h>
#include <sys/param.h>
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_eth.h"
#include "protocol_examples_common.h"
#include "esp_tls_crypto.h"
#include <esp_http_server.h>

#include "driver/gpio.h"
#include "driver/sdmmc_host.h"
#include "driver/sdmmc_defs.h"
#include "sdmmc_cmd.h"
#include "esp_vfs_fat.h"

#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

#include "esp_camera.h"

#include "esp_timer.h"

#include "cJSON.h"
/* A simple example that demonstrates how to create GET and POST
 * handlers for the web server.
 */
#define SD_CARD_MOUNT_POINT "/sdcard"

static const char *TAG = "example";
char selectedFolder[32] = "DEFAULT";

/*----------------------------------
---------- Kamera-------------------
-----------------------------------*/
// Funktion zum Initialisieren der Kamera
#define BUTTON_PIN 3

#define CAM_PIN_PWDN    32 
#define CAM_PIN_RESET   -1 //software reset will be performed
#define CAM_PIN_XCLK    0
#define CAM_PIN_SIOD    26
#define CAM_PIN_SIOC    27

#define CAM_PIN_D7      35
#define CAM_PIN_D6      34
#define CAM_PIN_D5      39
#define CAM_PIN_D4      36
#define CAM_PIN_D3      21
#define CAM_PIN_D2      19
#define CAM_PIN_D1      18
#define CAM_PIN_D0       5
#define CAM_PIN_VSYNC   25
#define CAM_PIN_HREF    23
#define CAM_PIN_PCLK    22

#define CONFIG_XCLK_FREQ 20000000 
#define CONFIG_OV2640_SUPPORT 1
#define CONFIG_OV7725_SUPPORT 1
#define CONFIG_OV3660_SUPPORT 1
#define CONFIG_OV5640_SUPPORT 1

static esp_err_t init_camera(void)
{
    camera_config_t camera_config = {
        .pin_pwdn  = CAM_PIN_PWDN,
        .pin_reset = CAM_PIN_RESET,
        .pin_xclk = CAM_PIN_XCLK,
        .pin_sccb_sda = CAM_PIN_SIOD,
        .pin_sccb_scl = CAM_PIN_SIOC,

        .pin_d7 = CAM_PIN_D7,
        .pin_d6 = CAM_PIN_D6,
        .pin_d5 = CAM_PIN_D5,
        .pin_d4 = CAM_PIN_D4,
        .pin_d3 = CAM_PIN_D3,
        .pin_d2 = CAM_PIN_D2,
        .pin_d1 = CAM_PIN_D1,
        .pin_d0 = CAM_PIN_D0,
        .pin_vsync = CAM_PIN_VSYNC,
        .pin_href = CAM_PIN_HREF,
        .pin_pclk = CAM_PIN_PCLK,

        .xclk_freq_hz = CONFIG_XCLK_FREQ,
        .ledc_timer = LEDC_TIMER_0,
        .ledc_channel = LEDC_CHANNEL_0,

        .pixel_format = PIXFORMAT_JPEG,
        .frame_size = FRAMESIZE_UXGA,

        .jpeg_quality = 12,
        .fb_count = 1,
        .grab_mode = CAMERA_GRAB_WHEN_EMPTY};//CAMERA_GRAB_LATEST. Sets when buffers should be filled
    esp_err_t err = esp_camera_init(&camera_config);
    if (err != ESP_OK)
    {
        return err;
    }
    return ESP_OK;
}

/*----------------------------------
---------- SD-Karte-------------------
-----------------------------------*/
static esp_err_t initi_sd_card(void)
{  
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 3,
    };
    sdmmc_card_t *card;
    esp_err_t err = esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot_config, &mount_config, &card);
    if (err != ESP_OK)
    {
        return err;
    }
    return ESP_OK;
}


int sd_card_count_files() {
    // Dateipfad auf der SD-Karte
    char folder_path[512];
    snprintf(folder_path, sizeof(folder_path), "/sdcard/%s", selectedFolder);
    // Öffne das Verzeichnis auf der SD-Karte
    DIR* dir = opendir(folder_path);
    if (!dir) {
        printf("Fehler beim Öffnen des Verzeichnisses auf der SD-Karte.\n");
        return -1;
    }

    // Zähle die Dateien mit dem angegebenen Präfix und Suffix
    int fileCount = 0;
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_REG && strncmp(entry->d_name, "PIC", strlen("PIC")) == 0 &&
            strstr(entry->d_name, ".JPG") != NULL) {
            fileCount++;
        }
    }
    ESP_LOGI(TAG, "Files gefunden: %d", fileCount);
    // Schließe das Verzeichnis
    closedir(dir);

    return fileCount;
}


static void take_photo()
{
    int nextImage = sd_card_count_files() + 1;
    ESP_LOGI(TAG, "Starting Taking Picture!\n");

    camera_fb_t *pic = esp_camera_fb_get();

    char photo_name[64];
    sprintf(photo_name, "/sdcard/%s/PIC_%d.JPG", selectedFolder, nextImage);
    // Time Photo taken: pic->timestamp.tv_sec
    FILE *file = fopen(photo_name, "w");
    if (file == NULL)
    {
        printf("err: fopen failed\n");
    }
    else
    {
        fwrite(pic->buf, 1, pic->len, file);
        fclose(file);
    }
    
    esp_camera_fb_return(pic);
    vTaskDelete(NULL);
    printf("Finished Taking Picture!\n");
}


esp_err_t root_handler(httpd_req_t *req)
{
    // Holen des Dateinamens aus dem Query-String
    char currentImage[8];
    int imageCount = sd_card_count_files();

    if (httpd_req_get_url_query_str(req, currentImage, sizeof(currentImage)) == ESP_OK)
    {

        char html_image[64];
        snprintf(html_image, sizeof(html_image), "<img src='/image?PIC_%s.JPG' style='width:100%%'/>", currentImage);

        if(atoi(currentImage) >= imageCount) {
            strcpy(currentImage, "0");
        }
        // HTML-Header
        char html_header[1024];  // Annahme: Ausreichende Größe für den Header
        snprintf(html_header, sizeof(html_header),
                 "<html><head><script>"
                 "var currentImage = %s;"
                 "function loadNextImage() {"
                 "  console.log('Button pressed');"
                 "  currentImage++;"
                 "  location.href = '/root?' + currentImage;"
                 "}"
                 "function createFolder() {"
                 "    var folderName = document.getElementById('folderName').value;"
                 "    if (folderName.trim() === '') {"
                 "        alert('Bitte geben Sie einen Ordnername ein.');"
                 "        return;"
                 "    }"
                 "    fetch('/create_folder?' + folderName, {"
                 "       method: 'GET'"
                 "    })"
                 "    .then(response => {"
                 "        if (response.ok) {"
                 "            alert('Ordner erfolgreich erstellt!');"
                 "        } else {"
                 "            alert('Fehler beim Erstellen des Ordners.');"
                 "        }"
                 "    })"
                 "    .catch(error => {"
                 "        console.error('Error:', error);"
                 "    });"
                 "}"
                 "</script></head><body><h1>ESP32-Cam Webserver</h1>",
                 currentImage);

        // Textfeld für den Ordnername
        const char *html_newFolder_text = "<input type='text' id='folderName' placeholder='Ordnername'>";

        // Button zum Erstellen des Ordners
        const char *html_newFolder_button = "<button onclick='createFolder()'>Ordner erstellen</button>";

        // HTML-Dropdown um Folder auszuwählen
        const char *html_dropdown_folder = "<script>"
                                           "function stopSerie() {"
                                           "    fetch('/get_folder_names')"
                                           "        .catch(error => {"
                                           "            console.error('Fehler beim Stoppen der Serie: ', error);"
                                           "        });"
                                           "}"
                                           "</script>"
                                           "<button onclick='stopSerie()'>Serie beenden</button>";

        char html_current_folder_info[64];
        snprintf(html_current_folder_info, sizeof(html_current_folder_info), "<p>Aktuell: %s</p>", selectedFolder);

        const char *html_stop_button = "<script>"
                                           "function stopSerie() {"
                                           "    fetch('/stop_serie')"
                                           "        .then(response => response.json())"
                                           "        .then(data => {"
                                           "            const folderSelect = document.getElementById('folderSelect');"
                                           "            folderSelect.innerHTML = '';"
                                           "            data.forEach(folderName => {"
                                           "                const option = document.createElement('option');"
                                           "                option.value = folderName;"
                                           "                option.text = folderName;"
                                           "                folderSelect.appendChild(option);"
                                           "            });"
                                           "        })"
                                           "        .catch(error => {"
                                           "            console.error('Fehler beim Laden der Ordnerliste:', error);"
                                           "        });"
                                           "}"
                                           "loadFolderNames();"
                                           "function changeSelectedFolder() {"
                                           "   const selectedFolder = document.getElementById('folderSelect').value;"
                                           "   fetch(`/change_selected_folder?${selectedFolder}`)"
                                           "   .catch(error => {"
                                           "       console.error('Fehler beim Laden der Bilder:', error);"
                                           "   });"
                                           "}"
                                           "</script>"
                                           "<label for='folderSelect'>Ordner: </label>"
                                           "<select id='folderSelect' onchange='changeSelectedFolder()'>"
                                           "</select>";

        // HTML-Button zum Laden des nächsten Bildes
        const char *html_next_button = "<button onclick='loadNextImage()'>Weiter</button>";

        // HTML-Footer
        const char *html_footer = "</body></html>";


        // Den Header senden
        httpd_resp_send_chunk(req, html_header, strlen(html_header));

        // Das Textfeld folderName
        httpd_resp_send_chunk(req, html_newFolder_text, strlen(html_newFolder_text));

        // Der Button neuer Ordner
        httpd_resp_send_chunk(req, html_newFolder_button, strlen(html_newFolder_button));

        // Dropdownmenü
        httpd_resp_send_chunk(req, html_dropdown_folder, strlen(html_dropdown_folder));

        // Aktueller Ordner
        httpd_resp_send_chunk(req, html_current_folder_info, strlen(html_current_folder_info));

        // Der Button stoppt Bildserie
        httpd_resp_send_chunk(req, html_stop_button, strlen(html_stop_button));

        // Der Button nächstes Bild
        httpd_resp_send_chunk(req, html_next_button, strlen(html_next_button));

        // Das Bild als HTML-Element einfügen
        httpd_resp_send_chunk(req, html_image, strlen(html_image));

        // HTML-Footer senden
        httpd_resp_send_chunk(req, html_footer, strlen(html_footer));

        // Stream beenden
        httpd_resp_send_chunk(req, NULL, 0);

        return ESP_OK;
    }
    else
    {
        ESP_LOGE(TAG, "Fehler beim Holen des Dateinamens aus dem Query-String");
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }
}

esp_err_t image_handler(httpd_req_t *req)
{
    // Holen des Dateinamens aus dem Query-String
    char filename[16];
    if (httpd_req_get_url_query_str(req, filename, sizeof(filename)) == ESP_OK)
    {
        // Dateipfad auf der SD-Karte
        char file_path[64];
        snprintf(file_path, sizeof(file_path), "/sdcard/%s/%s", selectedFolder, filename);

        // Öffne die Datei auf der SD-Karte
        FILE *file = fopen(file_path, "rb");
        if (file == NULL)
        {
            ESP_LOGE(TAG, "Fehler beim Öffnen der Datei: %s", file_path);
            httpd_resp_send_404(req);
            return ESP_FAIL;
        }

        // Sende den Header für das Bild
        httpd_resp_set_type(req, "image/jpeg");
        httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=bild.jpg");

        // Lese das Bild und sende es im Chunks an den Client
        char *chunk = (char *)malloc(1024);
        size_t read_len;
        do
        {
            read_len = fread(chunk, 1, 1024, file);
            if (read_len > 0)
            {
                if (httpd_resp_send_chunk(req, chunk, read_len) != ESP_OK)
                {
                    free(chunk);
                    fclose(file);
                    ESP_LOGE(TAG, "Fehler beim Senden des Bild-Chunks");
                    return ESP_FAIL;
                }
            }
        } while (read_len > 0);

        // Schließe die Datei und befreie den Chunk-Speicher
        free(chunk);
        fclose(file);

        // Beende den Bild-Stream
        httpd_resp_send_chunk(req, NULL, 0);

        return ESP_OK;
    }
    else
    {
        ESP_LOGE(TAG, "Fehler beim Holen des Dateinamens aus dem Query-String");
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }
}

esp_err_t createFolderHandler(httpd_req_t *req) {
    ESP_LOGI(TAG, "Entered creatFolderHandler");

    // Extrahiere den Ordnername aus den Query-Parametern
    char folderName[32];
    size_t folderNameLen = httpd_req_get_url_query_len(req);
    if (folderNameLen > 0) {
        if (httpd_req_get_url_query_str(req, folderName, sizeof(folderName)) == ESP_OK) {
            // Hier könntest du zusätzliche Überprüfungen durchführen oder die Ordnererstellung implementieren
            // Zum Beispiel: Verwende die Funktion mkdir, um einen Ordner zu erstellen
            char folderPath[256];
            snprintf(folderPath, sizeof(folderPath), "/sdcard/%s", folderName);
            int mkdirResult = mkdir(folderPath, 0777);

            if (mkdirResult == 0) {
                // Erfolgreiche Antwort senden
                httpd_resp_sendstr(req, "OK");
                return ESP_OK;
            }
        }
    }

    // Fehlerhafte Antwort senden
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Internal Server Error");
    return ESP_FAIL;

    // Fehlerhafte Antwort senden
    httpd_resp_send(req, "Bad Request", HTTPD_RESP_USE_STRLEN);
    return ESP_FAIL;
}


esp_err_t getFolderNamesHandler(httpd_req_t *req) {
    // Öffne das Verzeichnis auf der SD-Karte
    DIR *dir = opendir("/sdcard");  // Passe den Pfad entsprechend an
    if (!dir) {
        ESP_LOGE(TAG, "Failed to open directory");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Internal Server Error");
        return ESP_FAIL;
    }

    cJSON *folderArray = cJSON_CreateArray();
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_DIR) {
            // Ignoriere die speziellen Verzeichnisse "." und ".."
            if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
                cJSON_AddItemToArray(folderArray, cJSON_CreateString(entry->d_name));
            }
        }
    }

    closedir(dir);

    const char *folderNamesJson = cJSON_PrintUnformatted(folderArray);
    cJSON_Delete(folderArray);

    // Antworte mit dem JSON-Array der Ordner
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, folderNamesJson);

    // Freigabe des JSON-Strings
    free((void *)folderNamesJson);

    return ESP_OK;
}

esp_err_t changeSelectedFolderHandler(httpd_req_t *req)
{
    // Holen des Dateinamens aus dem Query-String
    char foldername[32];
    if (httpd_req_get_url_query_str(req, foldername, sizeof(foldername)) == ESP_OK)
    {
        if (strlen(foldername) < sizeof(selectedFolder)) {
            strcpy(selectedFolder, foldername);
            ESP_LOGI(TAG, "selectedFolder: %s", foldername);
            return ESP_OK;
        } else {
            ESP_LOGE(TAG, "Fehler beim Ändern des Ordners");
            httpd_resp_send_404(req);
            return ESP_FAIL;
        }
    }
    else
    {
        ESP_LOGE(TAG, "Fehler beim Ändern des Ordners");
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }
}


TaskHandle_t taskHandle;

esp_err_t startPhotoSerieHandler(httpd_req_t *req)
{
   // Lösche die Aufgabe
    vTaskDelete(taskHandle);
    return ESP_OK;
}


esp_err_t stopPhotoSerieHandler(httpd_req_t *req)
{
   // Lösche die Aufgabe
    vTaskDelete(taskHandle);
    return ESP_OK;
}

httpd_uri_t root = {
    .uri       = "/root",
    .method    = HTTP_GET,
    .handler   = root_handler,
    .user_ctx  = NULL
};

static const httpd_uri_t image = {
    .uri       = "/image",
    .method    = HTTP_GET,
    .handler   = image_handler,
    .user_ctx  = NULL
};


static const httpd_uri_t createFolderUri = {
    .uri       = "/create_folder",
    .method    = HTTP_GET,
    .handler   = createFolderHandler,
    .user_ctx  = NULL
};

static const httpd_uri_t getFolderNamesEndpoint = {
    .uri       = "/get_folder_names",
    .method    = HTTP_GET,
    .handler   = getFolderNamesHandler,
    .user_ctx  = NULL
};

static const httpd_uri_t change_selected_folder = {
    .uri       = "/change_selected_folder",
    .method    = HTTP_GET,
    .handler   = changeSelectedFolderHandler,
    .user_ctx  = NULL
};

static const httpd_uri_t stop_serie = {
    .uri       = "/stop_serie",
    .method    = HTTP_GET,
    .handler   = stopPhotoSerieHandler,
    .user_ctx  = NULL
};

esp_err_t http_404_error_handler(httpd_req_t *req, httpd_err_code_t err)
{
    if (strcmp("/image", req->uri) == 0) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "/image URI is not available");
        /* Return ESP_OK to keep underlying socket open */
        return ESP_OK;
    } 
    /* For any other URI send 404 and close socket */
    httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Some 404 error message");
    return ESP_FAIL;
}

static httpd_handle_t start_webserver(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;

    // Start the httpd server
    ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK) {
        // Set URI handlers
        ESP_LOGI(TAG, "Registering URI handlers");
        httpd_register_uri_handler(server, &root);
        httpd_register_uri_handler(server, &image);
        httpd_register_uri_handler(server, &createFolderUri);
        httpd_register_uri_handler(server, &getFolderNamesEndpoint);
        httpd_register_uri_handler(server, &change_selected_folder);
        httpd_register_uri_handler(server, &stop_serie);
        return server;
    }

    ESP_LOGI(TAG, "Error starting server!");
    return NULL;
}

static esp_err_t stop_webserver(httpd_handle_t server)
{
    // Stop the httpd server
    return httpd_stop(server);
}

static void disconnect_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    httpd_handle_t* server = (httpd_handle_t*) arg;
    if (*server) {
        ESP_LOGI(TAG, "Stopping webserver");
        if (stop_webserver(*server) == ESP_OK) {
            *server = NULL;
        } else {
            ESP_LOGE(TAG, "Failed to stop http server");
        }
    }
}

static void connect_handler(void* arg, esp_event_base_t event_base,
                            int32_t event_id, void* event_data)
{
    httpd_handle_t* server = (httpd_handle_t*) arg;
    if (*server == NULL) {
        ESP_LOGI(TAG, "Starting webserver");
        *server = start_webserver();
    }
}

// Zählvariable für die Anzahl der Fotos
int photoCount = 0;

// Thread für das Aufnehmen von Fotos
void photoTask() {
    while (photoCount < 10) {
        take_photo();
        photoCount++;
        vTaskDelay(30000 / portTICK_PERIOD_MS); // Warte 1 Sekunde zwischen den Fotos
    }
    vTaskDelete(NULL); // Beende den Task nach 10 Fotos
}

void codeForTask2_core( void * parameter )
{
   for (;;)
   {
      printf("codeForTask2 is running on Core: ");
      printf("%d\n", xPortGetCoreID());
      vTaskDelay(20000 / portTICK_PERIOD_MS);
   }

}


void app_main(void)
{
    esp_err_t err;
    err = init_camera();
    if (err != ESP_OK)
    {
        printf("err: %s\n", esp_err_to_name(err));
        return;
    }
    
    err = initi_sd_card();
    if (err != ESP_OK)
    {
        printf("err: %s\n", esp_err_to_name(err));
        return;
    }

    static httpd_handle_t server = NULL;

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* This helper function configures Wi-Fi or Ethernet, as selected in menuconfig.
     * Read "Establishing Wi-Fi or Ethernet Connection" section in
     * examples/protocols/README.md for more information about this function.
     */
    ESP_ERROR_CHECK(example_connect());

    /* Register event handlers to stop the server when Wi-Fi or Ethernet is disconnected,
     * and re-start it upon connection.
     */
#ifdef CONFIG_EXAMPLE_CONNECT_WIFI
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &connect_handler, &server));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &disconnect_handler, &server));
#endif // CONFIG_EXAMPLE_CONNECT_WIFI

    /* Start the server for the first time */
    server = start_webserver();

    xTaskCreatePinnedToCore(codeForTask2_core, "core1", 1024*2, NULL, 2, &taskHandle, 1);
}

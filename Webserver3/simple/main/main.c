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

#include "esp_timer.h"

#include "cJSON.h"

#include "sd_card.h"
#include "cam.h"

/* A simple example that demonstrates how to create GET and POST
 * handlers for the web server.
 */

static const char *TAG = "main";
char selectedFolder[32] = "DEFAULT";
char serieFolder[32] = "";
int serieRange = 0; // Seriendauer in Tagen
int serieFreuqenz = 0; // Zeit zwischen Bildern in Stunden
bool isSerieRunning = false;
time_t start_time;

int get_rest_time(){
    time_t current_time;
    time(&current_time);

    int rest_time = 1;

    rest_time = (serieRange * 24 * 60 * 60) - (current_time - start_time);

    return rest_time;
}


esp_err_t root_handler(httpd_req_t *req)
{
    // Holen des Dateinamens aus dem Query-String
    char currentImage[8];
    int imageCount = sd_card_count_files(selectedFolder);

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
                 "function loadNextImage(i) {"
                 "  console.log('Button pressed');"
                 "  if(currentImage <= 1 && i <= 0) {"
                 "      currentImage = 1;"
                 "  } else {"
                 "      currentImage = currentImage + i;"
                 "  }"
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

        // Textfeld für Seriendauer
        const char *html_serieRange_text = "Dauer: <input type='number' min='1' id='serieRange' placeholder='Serien Dauer'> in Tagen. ";

        // Textfeld für Serienfrequenz
        const char *html_serieFrequenz_text = " Frequenz: <input type='number' min='1' id='serieFrequenz' placeholder='Frequenz'> in Stunden. ";

        int seconds_lasting = get_rest_time();

        int days_lasting = seconds_lasting / (24 * 60 * 60);
        seconds_lasting = seconds_lasting % (24 * 60 * 60);
    
        int hours_lasting = seconds_lasting / (60 * 60);
        seconds_lasting = seconds_lasting % (60 * 60);

        int minutes_lasting = seconds_lasting / 60;
        int seconds = seconds_lasting % 60;

        // Verbleibende Zeit
        char html_lastingTime[128];
        snprintf(html_lastingTime, sizeof(html_lastingTime), 
                "Verbleibende Zeit: %d Tage, %d Stunden, %d Minuten, %d Sekunden\n", 
                days_lasting, hours_lasting, minutes_lasting, seconds);

        // Button um serie zu stoppen
        const char *html_stop_button = "<script>"
                                       "function stopSerie() {"
                                       "    fetch('/stop_serie')"
                                       "        .then(response => {"
                                       "            if (response.ok) {"
                                       "                alert('Serie beendet.');"
                                       "                location.reload();"
                                       "            } else {"
                                       "                alert('Serie konnte nicht beendet werden.');"
                                       "            }"
                                       "        })"
                                       "        .catch(error => {"
                                       "            console.error('Fehler beim Stoppen der Serie: ', error);"
                                       "        });"
                                       "}"
                                       "</script>"
                                       "<button onclick='stopSerie()'>Serie beenden</button>";

        // Button um serie zu starten
        const char *html_start_button     = "<script>"
                                           "function startSerie() {"
                                           "    var serieRange = document.getElementById('serieRange').value;"
                                           "    var serieFrequenz = document.getElementById('serieFrequenz').value;"
                                           "    if (serieRange.trim() === '') {"
                                           "        alert('Bitte geben Sie eine Dauer ein.');"
                                           "        return;"
                                           "    }"
                                           "    if (serieFrequenz.trim() === '') {"
                                           "        alert('Bitte geben Sie eine Frequenz ein.');"
                                           "        return;"
                                           "    }"
                                           "    fetch('/start_serie?range=' + serieRange + 'frequenz=' + serieFrequenz)"
                                           "        .then(response => {"
                                           "            if (response.ok) {"
                                           "                alert('Serie gestartet');"
                                           "                location.reload();"
                                           "            } else {"
                                           "                alert('Serie konnte nicht gestartet werden.');"
                                           "            }"
                                           "        })"
                                           "        .catch(error => {"
                                           "            console.error('Fehler beim Starten der Serie: ', error);"
                                           "        });"
                                           "}"
                                           "</script>"
                                           "<button onclick='startSerie()'>Serie starten</button>";

        char html_current_folder_info[64];
        snprintf(html_current_folder_info, sizeof(html_current_folder_info), "Aktuell: %s ", selectedFolder);

        const char *html_dropdown_folder = "<script>"
                                           "function loadFolderNames() {"
                                           "    fetch('/get_folder_names')"
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
                                           "    .then(response => {"
                                           "            if (response.ok) {"
                                           "                location.reload();"
                                           "            } else {"
                                           "                alert('Odner konnte nicht gewechselt werden.');"
                                           "            }"
                                           "        })"
                                           "   .catch(error => {"
                                           "       console.error('Fehler beim Laden der Bilder:', error);"
                                           "   });"
                                           "}"
                                           "</script>"
                                           "<label for='folderSelect'>Ordner: </label>"
                                           "<select id='folderSelect' onchange='changeSelectedFolder()'>"
                                           "</select>"
                                           "<script>"
                                           "const folderSelect = document.getElementById('folderSelect');"
                                           "folderSelect.value = 'DEFAULT';"
                                           "</script>";

        // HTML-Button zum Laden des nächsten Bildes
        const char *html_next_button = "<button onclick='loadNextImage(1)'>Weiter</button>";

        // HTML-Button zum Laden des vorhergehenden Bildes
        const char *html_back_button = "<button onclick='loadNextImage(-1)'>Zurueck</button>";

        //HTML-Zeilensprung
        const char *html_jump = "<br>";

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

        // Zeilensprung
        httpd_resp_send_chunk(req, html_jump, strlen(html_jump));

        if(isSerieRunning){
            // Verbleibende Zeit 
            httpd_resp_send_chunk(req, html_lastingTime, strlen(html_lastingTime));
            // Der Button stoppt Bildserie
            httpd_resp_send_chunk(req, html_stop_button, strlen(html_stop_button));
        } else {
            // Textfeld Serien Dauer
            httpd_resp_send_chunk(req, html_serieRange_text, strlen(html_serieRange_text));

            // Textfeld Serien Frequenz
            httpd_resp_send_chunk(req, html_serieFrequenz_text, strlen(html_serieFrequenz_text));
            // Der Button startet Bildserie
            httpd_resp_send_chunk(req, html_start_button, strlen(html_start_button));
        }

        // Zeilensprung
        httpd_resp_send_chunk(req, html_jump, strlen(html_jump));

        // Aktueller Ordner
        httpd_resp_send_chunk(req, html_current_folder_info, strlen(html_current_folder_info));

        // Der Button vorhergehenden Bild
        httpd_resp_send_chunk(req, html_back_button, strlen(html_back_button));

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
        snprintf(file_path, sizeof(file_path), "%s/%s/%s", SD_CARD_MOUNT_POINT, selectedFolder, filename);

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
            snprintf(folderPath, sizeof(folderPath), "%s/%s", SD_CARD_MOUNT_POINT, folderName);
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
    DIR *dir = opendir(SD_CARD_MOUNT_POINT);  // Passe den Pfad entsprechend an
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
            httpd_resp_sendstr(req, "OK");
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

void photo_task(void *pvParameters)
{
    strcpy(serieFolder, selectedFolder);

    take_photo(serieFolder);

    int repetitions = (24 * serieRange) / serieFreuqenz; //in Tagen und Stunden
    //int repetitions = serieRange / serieFreuqenz; //in Minuten und Minuten

    for (int i = 0; i<repetitions; i++)
    {
        vTaskDelay((3600000*serieFreuqenz) / portTICK_PERIOD_MS); //in Stunden
        //vTaskDelay((60000*serieFreuqenz) / portTICK_PERIOD_MS); //in Minuten
        take_photo(serieFolder);
    }
    vTaskDelete(taskHandle);
    isSerieRunning = false;
}

esp_err_t startPhotoSerieHandler(httpd_req_t *req)
{
    char urlParameter[64];
    if (httpd_req_get_url_query_str(req, urlParameter, sizeof(urlParameter)) == ESP_OK) {
        ESP_LOGI(TAG, "Angekommen: %s", urlParameter);

        // Suche nach dem "range="-Präfix und extrahiere die Zahl
        const char *rangePtr = strstr(urlParameter, "range=");
        if (rangePtr != NULL) {
            serieRange = atoi(rangePtr + strlen("range="));
        }

        // Suche nach dem "frequenz="-Präfix und extrahiere die Zahl
        const char *frequenzPtr = strstr(urlParameter, "frequenz=");
        if (frequenzPtr != NULL) {
            serieFreuqenz = atoi(frequenzPtr + strlen("frequenz="));
        }
    }

    time(&start_time);

    xTaskCreatePinnedToCore(photo_task, "core1", 1024*3, NULL, 2, &taskHandle, 1);

    // Erfolgreiche Antwort senden
    isSerieRunning = true;
    httpd_resp_sendstr(req, "OK");
    return ESP_OK;
}

esp_err_t stopPhotoSerieHandler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Beendet Serie");
    // Lösche die Aufgabe
    vTaskDelete(taskHandle);
    isSerieRunning = false;
    // Erfolgreiche Antwort senden
    httpd_resp_sendstr(req, "OK");
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

static const httpd_uri_t start_serie = {
    .uri       = "/start_serie",
    .method    = HTTP_GET,
    .handler   = startPhotoSerieHandler,
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
        httpd_register_uri_handler(server, &start_serie);
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
    
    server = start_webserver();    
}
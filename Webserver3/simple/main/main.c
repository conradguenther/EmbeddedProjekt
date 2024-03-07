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
/* A simple example that demonstrates how to create GET and POST
 * handlers for the web server.
 */

static const char *TAG = "example";

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

esp_err_t root_handler(httpd_req_t *req)
{
    // HTML-Header
    const char *html_header = "<html><body><h1>ESP32-Cam Webserver</h1>";
    
    // HTML-Footer
    const char *html_footer = "</body></html>";
    
    // Den Header senden
    httpd_resp_send_chunk(req, html_header, strlen(html_header));

    // Das Bild als HTML-Element einfügen
    httpd_resp_send_chunk(req, "<img src='/image' style='width:100%'/>", strlen("<img src='/image' style='width:100%'/>"));

    // HTML-Footer senden
    httpd_resp_send_chunk(req, html_footer, strlen(html_footer));

    // Stream beenden
    httpd_resp_send_chunk(req, NULL, 0);

    return ESP_OK;
}

// Dateipfad auf der SD-Karte
#define FILE_PATH "/sdcard/PIC_4.JPG"

esp_err_t image_handler(httpd_req_t *req)
{
    // Dateipfad auf der SD-Karte
    const char *file_path = FILE_PATH;

    // Öffne die Datei auf der SD-Karte
    FILE *file = fopen(file_path, "rb");
    if (file == NULL)
    {
        ESP_LOGE(TAG, "Fehler beim Öffnen der Datei");
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    // Sende den Header für das Bild
    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=PIC_4.JPG");

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

// URI-Handler-Struktur
httpd_uri_t root = {
    .uri       = "/",
    .method    = HTTP_GET,
    .handler   = root_handler,
    .user_ctx  = NULL
};

static const httpd_uri_t image = {
    .uri       = "/image",
    .method    = HTTP_GET,
    .handler   = image_handler,
    /* Let's pass response string in user
     * context to demonstrate it's usage */
    .user_ctx  = NULL
};

/* This handler allows the custom error handling functionality to be
 * tested from client side. For that, when a PUT request 0 is sent to
 * URI /ctrl, the /image and /echo URIs are unregistered and following
 * custom error handler http_404_error_handler() is registered.
 * Afterwards, when /image or /echo is requested, this custom error
 * handler is invoked which, after sending an error message to client,
 * either closes the underlying socket (when requested URI is /echo)
 * or keeps it open (when requested URI is /image). This allows the
 * client to infer if the custom error handler is functioning as expected
 * by observing the socket state.
 */
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
        httpd_register_uri_handler(server, &image);;

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
    initi_sd_card();

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
}

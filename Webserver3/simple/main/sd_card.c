#include "sd_card.h"
#include "driver/sdmmc_host.h"
#include "esp_vfs_fat.h"
#include <dirent.h>
#include <esp_log.h>
#include <string.h>

/*----------------------------------
---------- SD-Karte-------------------
-----------------------------------*/
static const char *TAG = "sd_card";

esp_err_t initi_sd_card(void)
{  
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 3,
    };
    sdmmc_card_t *card;
    esp_err_t err = esp_vfs_fat_sdmmc_mount(SD_CARD_MOUNT_POINT, &host, &slot_config, &mount_config, &card);
    if (err != ESP_OK)
    {
        return err;
    }
    return ESP_OK;
}

int sd_card_count_files(char *folder) {
    // Dateipfad auf der SD-Karte
    char folder_path[64];
    snprintf(folder_path, sizeof(folder_path), "%s/%s", SD_CARD_MOUNT_POINT, folder);
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
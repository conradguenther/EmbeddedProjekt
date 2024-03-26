#ifndef SD_CARD_H
#define SD_CARD_H

#include "esp_err.h"

#define SD_CARD_MOUNT_POINT "/sdcard"

esp_err_t initi_sd_card(void);

int sd_card_count_files(char *folder);

#endif // SD_CARD_H
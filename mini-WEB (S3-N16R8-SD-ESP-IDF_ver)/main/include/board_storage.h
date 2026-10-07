#pragma once
#include "sdkconfig.h"
#if !defined(CONFIG_IDF_TARGET_ESP32S3)
#error "S3 N16R8 SD requires esp32s3. Run idf.py set-target esp32s3 in this project."
#endif
#include "driver/gpio.h"
/* Native SDMMC, one data line. GPIO35..37 belong to octal PSRAM. */
#define BOARD_SD_CMD GPIO_NUM_38
#define BOARD_SD_CLK GPIO_NUM_39
#define BOARD_SD_D0  GPIO_NUM_40

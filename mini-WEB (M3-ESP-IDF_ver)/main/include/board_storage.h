#pragma once
#include "sdkconfig.h"
#if !defined(CONFIG_IDF_TARGET_ESP32P4)
#error "P4 requires esp32p4. Run idf.py set-target esp32p4 in this project."
#endif
#include "driver/gpio.h"
/* GUITION M3: SDMMC slot 0, 4-bit. VO4 supplies TF power.
   GPIO45 is not connected on this profile; never drive it. */
#define BOARD_SD_CMD GPIO_NUM_44
#define BOARD_SD_CLK GPIO_NUM_43
#define BOARD_SD_D0 GPIO_NUM_39
#define BOARD_SD_LDO_CHANNEL 4
#define BOARD_SD_D1 GPIO_NUM_40
#define BOARD_SD_D2 GPIO_NUM_41
#define BOARD_SD_D3 GPIO_NUM_42

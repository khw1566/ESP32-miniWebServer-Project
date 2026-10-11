#pragma once
#include "sdkconfig.h"
#if !defined(CONFIG_IDF_TARGET_ESP32P4)
#error "P4 requires esp32p4. Run idf.py set-target esp32p4 in this project."
#endif
#include "driver/gpio.h"
/* Waveshare Module DEV KIT schematic: TF CMD44, CLK43, D0=39.
   Slot uses 1-bit mode at 10 MHz. LDO4 supplies SD IO at 3.3V. */
#define BOARD_SD_CMD GPIO_NUM_44
#define BOARD_SD_CLK GPIO_NUM_43
#define BOARD_SD_D0 GPIO_NUM_39
#define BOARD_SD_LDO_CHANNEL 4
#define BOARD_SD_POWER_EN GPIO_NUM_45

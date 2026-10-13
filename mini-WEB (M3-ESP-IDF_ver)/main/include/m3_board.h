#pragma once
#include "driver/i2c_master.h"
#include "esp_lvgl_port.h"
/* GUITION JC4880P443C_I_W, JC-ESP32P4-M3 core module. */
#define M3_WIDTH 480
#define M3_HEIGHT 800
#define M3_LCD_RESET 5
#define M3_LCD_BACKLIGHT 23
#define M3_I2C_SDA 7
#define M3_I2C_SCL 8
#define M3_I2S_MCLK 13
#define M3_I2S_BCLK 12
#define M3_I2S_LRCK 10
#define M3_I2S_DOUT 9
#define M3_I2S_DIN 48
#define M3_AMP_ENABLE 11
esp_err_t m3_board_init(void);
i2c_master_bus_handle_t m3_board_i2c(void);
lv_display_t *m3_board_display(void);
bool m3_board_touch_ready(void);

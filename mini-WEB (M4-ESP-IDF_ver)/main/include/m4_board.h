#pragma once
#include "driver/i2c_master.h"
#include "esp_lvgl_port.h"
/* GUITION JC1060P470C. Panel scanout is landscape; LVGL rotates to portrait.
 * M4_WIDTH/HEIGHT describe the inherited app design canvas, not the panel. */
#define M4_WIDTH 480
#define M4_HEIGHT 800
#define M4_PANEL_WIDTH 1024
#define M4_PANEL_HEIGHT 600
#define M4_DISPLAY_WIDTH 600
#define M4_DISPLAY_HEIGHT 1024
#define M4_LCD_RESET CONFIG_M4_LCD_RESET
#define M4_LCD_BACKLIGHT CONFIG_M4_LCD_BACKLIGHT
#define M4_I2C_SDA 7
#define M4_I2C_SCL 8
#define M4_I2S_MCLK CONFIG_M4_I2S_MCLK
#define M4_I2S_BCLK CONFIG_M4_I2S_BCLK
#define M4_I2S_LRCK CONFIG_M4_I2S_LRCK
#define M4_I2S_DOUT CONFIG_M4_I2S_DOUT
#define M4_I2S_DIN CONFIG_M4_I2S_DIN
#define M4_AMP_ENABLE 11
esp_err_t m4_board_init(void);
i2c_master_bus_handle_t m4_board_i2c(void);
lv_display_t *m4_board_display(void);
bool m4_board_touch_ready(void);
esp_err_t m4_board_set_brightness(unsigned percent);

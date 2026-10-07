#pragma once
#include "esp_err.h"
#include "lvgl.h"
/* Standard ESP32-2432S028R. Reset is tied to board reset. */
#define M1_LCD_MOSI 13
#define M1_LCD_CLK 14
#define M1_LCD_CS 15
#define M1_LCD_DC 2
#define M1_LCD_BL 21
/* TFT_eSPI ILI9341_2_DRIVER, portrait rotation 0, invertDisplay(true). */
#define M1_LCD_INVERT_COLOR 1
#define M1_LCD_MIRROR_X 1
#define M1_LCD_MIRROR_Y 0
#define M1_LCD_SPI_HZ 26000000
#define M1_LCD_WIDTH 240
#define M1_LCD_HEIGHT 320
#define M1_LCD_BUFFER_LINES 8
#define M1_TOUCH_MOSI 32
#define M1_TOUCH_MISO 39
#define M1_TOUCH_CLK 25
#define M1_TOUCH_CS 33
#define M1_TOUCH_IRQ 36
#define M1_SD_MOSI 23
#define M1_SD_MISO 19
#define M1_SD_CLK 18
#define M1_SD_CS 5
/* Portrait raw axes. Tune against the physical board. */
#define M1_TOUCH_X_MIN 200
#define M1_TOUCH_X_MAX 3900
#define M1_TOUCH_Y_MIN 200
#define M1_TOUCH_Y_MAX 3900
#define M1_TOUCH_SWAP_XY 0
/* Portrait touch is mirrored across the vertical center line. */
#define M1_TOUCH_INVERT_X 1
#define M1_TOUCH_INVERT_Y 0
esp_err_t board_display_init(void);
/* Screen saver support: panel off, backlight off, LVGL draw buffer freed. */
esp_err_t board_display_sleep(void);
esp_err_t board_display_wake(void);
void board_display_backlight(bool on);
bool board_display_touch_down(void);
void onboard_ui_start(void);
extern const lv_font_t font_m1;

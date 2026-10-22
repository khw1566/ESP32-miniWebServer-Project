#include "m3_board.h"
#include "m3_panel_init.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "m3_preferences.h"
#include "esp_check.h"
#include "esp_ldo_regulator.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_touch_gt911.h"
#include "freertos/task.h"
#ifndef CONFIG_M3_TOUCH_MIRROR_X
#define CONFIG_M3_TOUCH_MIRROR_X 0
#endif
#ifndef CONFIG_M3_TOUCH_MIRROR_Y
#define CONFIG_M3_TOUCH_MIRROR_Y 0
#endif

static const char *TAG = "M3_BOARD";
#include "m3_power.h"
static i2c_master_bus_handle_t bus;
static lv_display_t *display;
static bool touch_ready;
static bool backlight_pwm_ready;
static esp_lcd_touch_io_gt911_config_t touch_address;
static esp_ldo_channel_handle_t phy_power;
i2c_master_bus_handle_t m3_board_i2c(void) { return bus; }
lv_display_t *m3_board_display(void) { return display; }
bool m3_board_touch_ready(void) { return touch_ready; }
esp_err_t m3_board_set_brightness(unsigned percent) {
    if (!backlight_pwm_ready) return ESP_ERR_INVALID_STATE;
    if (percent < 5) percent = 5;
    if (percent > 100) percent = 100;
    esp_err_t err = ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, percent * 1023U / 100U);
    if (err == ESP_OK) err = ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    if (err != ESP_OK) { ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 1); backlight_pwm_ready = false; }
    return err;
}

static esp_err_t touch_start(void) {
    if (CONFIG_M3_TOUCH_RESET >= 0) {
        ESP_RETURN_ON_ERROR(gpio_set_direction(CONFIG_M3_TOUCH_RESET, GPIO_MODE_OUTPUT), TAG, "touch reset");
        gpio_set_level(CONFIG_M3_TOUCH_RESET, 0);
        vTaskDelay(pdMS_TO_TICKS(10));
        gpio_set_level(CONFIG_M3_TOUCH_RESET, 1);
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    uint8_t address = 0x5d;
    if (i2c_master_probe(bus, address, 100) != ESP_OK) address = 0x14;
    ESP_RETURN_ON_ERROR(i2c_master_probe(bus, address, 100), TAG, "GT911 not found");
    esp_lcd_panel_io_i2c_config_t io_cfg = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
    io_cfg.dev_addr = address;
    io_cfg.scl_speed_hz = 400000;
    esp_lcd_panel_io_handle_t io;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(bus, &io_cfg, &io), TAG, "touch IO");
    esp_lcd_touch_config_t cfg = {
        .x_max = M3_WIDTH, .y_max = M3_HEIGHT,
        /* Reset was completed before probing; do not change the selected address. */
        .rst_gpio_num = GPIO_NUM_NC, .int_gpio_num = GPIO_NUM_NC,
        .flags = {.mirror_x = CONFIG_M3_TOUCH_MIRROR_X, .mirror_y = CONFIG_M3_TOUCH_MIRROR_Y},
        .driver_data = &touch_address
    };
    touch_address.dev_addr = address;
    esp_lcd_touch_handle_t touch;
    ESP_RETURN_ON_ERROR(esp_lcd_touch_new_i2c_gt911(io, &cfg, &touch), TAG, "GT911");
    lvgl_port_touch_cfg_t input = {.disp = display, .handle = touch};
    return lvgl_port_add_touch(&input) ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t m3_board_init(void) {
    /* Keep backlight and amplifier quiet while peripherals are being configured. */
    ESP_RETURN_ON_ERROR(gpio_set_direction(M3_LCD_BACKLIGHT, GPIO_MODE_OUTPUT), TAG, "backlight");
    gpio_set_level(M3_LCD_BACKLIGHT, 0);
    ESP_RETURN_ON_ERROR(gpio_set_direction(M3_AMP_ENABLE, GPIO_MODE_OUTPUT), TAG, "amplifier");
    gpio_set_level(M3_AMP_ENABLE, 0);
    i2c_master_bus_config_t i2c = {
        .i2c_port = I2C_NUM_0, .sda_io_num = M3_I2C_SDA, .scl_io_num = M3_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT, .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&i2c, &bus), TAG, "shared touch/codec I2C");
    esp_ldo_channel_config_t power = {.chan_id = 3, .voltage_mv = 2500};
    ESP_RETURN_ON_ERROR(esp_ldo_acquire_channel(&power, &phy_power), TAG, "DSI power");
    esp_lcd_dsi_bus_config_t dsi = {
        .bus_id = 0, .num_data_lanes = 2,
        /* IDF selects the revision-specific PLL reference (XTAL on P4 v3.x).
         * The old MIPI_DSI_PHY_CLK_SRC_DEFAULT alias forces legacy PLL_F20M
         * and aborts in the v3.x HAL. Zero requests the driver's default. */
        .phy_clk_src = 0,
        .lane_bit_rate_mbps = 500
    };
    esp_lcd_dsi_bus_handle_t dsi_bus;
    ESP_RETURN_ON_ERROR(esp_lcd_new_dsi_bus(&dsi, &dsi_bus), TAG, "DSI bus");
    esp_lcd_dbi_io_config_t dbi = {.virtual_channel = 0, .lcd_cmd_bits = 8, .lcd_param_bits = 8};
    esp_lcd_panel_io_handle_t io;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_dbi(dsi_bus, &dbi, &io), TAG, "DBI");
    esp_lcd_dpi_panel_config_t dpi = {
        .virtual_channel = 0, .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
        .dpi_clock_freq_mhz = 34, .pixel_format = LCD_COLOR_PIXEL_FORMAT_RGB565,
        .num_fbs = 2,
        .video_timing = {
            .h_size = M3_WIDTH, .v_size = M3_HEIGHT,
            .hsync_pulse_width = 12, .hsync_back_porch = 42, .hsync_front_porch = 42,
            .vsync_pulse_width = 2, .vsync_back_porch = 8, .vsync_front_porch = 166
        },
        .flags.use_dma2d = true
    };
    esp_lcd_panel_handle_t panel;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_dpi(dsi_bus, &dpi, &panel), TAG, "DPI");
    ESP_RETURN_ON_ERROR(gpio_set_direction(M3_LCD_RESET, GPIO_MODE_OUTPUT), TAG, "LCD reset");
    gpio_set_level(M3_LCD_RESET, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(M3_LCD_RESET, 1);
    vTaskDelay(pdMS_TO_TICKS(120));
    for (size_t i = 0; i < BOARD_P4_PANEL_INIT_CMDS_SIZE; ++i) {
        const board_p4_panel_init_cmd_t *cmd = &board_p4_panel_init_cmds[i];
        ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, cmd->cmd, cmd->data, cmd->len), TAG, "ST7701 init");
        if (cmd->delay_ms) vTaskDelay(pdMS_TO_TICKS(cmd->delay_ms));
    }
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(panel), TAG, "DPI init");
    lvgl_port_cfg_t port = ESP_LVGL_PORT_INIT_CONFIG();
    port.task_affinity = 1;
    /* All UI callbacks and the 250 ms refresh run on this task. 8 KiB overflowed
     * (canary panic -> reboot) once voice results, labels and float formatting nested.
     * Internal RAM on purpose: UI handlers write NVS, which is unsafe on a PSRAM stack. */
    port.task_stack = 16384;
    port.task_max_sleep_ms = 20;
    ESP_RETURN_ON_ERROR(lvgl_port_init(&port), TAG, "LVGL");
    lvgl_port_display_cfg_t lcd = {
        .panel_handle = panel, .buffer_size = M3_WIDTH * M3_HEIGHT,
        .double_buffer = true, .hres = M3_WIDTH, .vres = M3_HEIGHT,
        .color_format = LV_COLOR_FORMAT_RGB565,
        /* Direct mode redraws only changed areas (LVGL copies them into the other frame
         * buffer). Full refresh re-rendered all 480x800 pixels for every video frame. */
        .flags = {.buff_spiram = true, .direct_mode = true}
    };
    lvgl_port_display_dsi_cfg_t sync = {.flags.avoid_tearing = true};
    display = lvgl_port_add_disp_dsi(&lcd, &sync);
    if (!display) return ESP_ERR_NO_MEM;
    esp_err_t touch_error = touch_start();
    touch_ready = touch_error == ESP_OK;
    if (!touch_ready) ESP_LOGW(TAG, "Touch unavailable: %s; web setup remains available", esp_err_to_name(touch_error));
    /* MP3202 EN dimming needs <=1 kHz (datasheet Figure 7); use 500 Hz. */
    ledc_timer_config_t pwm_timer = {.speed_mode = LEDC_LOW_SPEED_MODE, .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = LEDC_TIMER_0, .freq_hz = 500, .clk_cfg = LEDC_AUTO_CLK};
    ledc_channel_config_t pwm_channel = {.gpio_num = M3_LCD_BACKLIGHT, .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0, .timer_sel = LEDC_TIMER_0, .duty = 1023};
    backlight_pwm_ready = ledc_timer_config(&pwm_timer) == ESP_OK && ledc_channel_config(&pwm_channel) == ESP_OK;
    m3_preferences_t preferences; m3_preferences_get(&preferences);
    if (!backlight_pwm_ready || m3_board_set_brightness(preferences.brightness) != ESP_OK) {
        gpio_reset_pin(M3_LCD_BACKLIGHT);
        gpio_set_direction(M3_LCD_BACKLIGHT, GPIO_MODE_OUTPUT);
        gpio_set_level(M3_LCD_BACKLIGHT, 1);
        backlight_pwm_ready = false;
        ESP_LOGW(TAG, "Backlight PWM unavailable; retaining full brightness");
    }
    m3_power_init(); /* Read-only diagnostic before service/audio workers. */
    return ESP_OK;
}

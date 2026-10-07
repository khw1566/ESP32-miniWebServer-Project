#include "board_display.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_ili9341.h"
#include "esp_heap_caps.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "esp_check.h"
#include <string.h>
#include <stdlib.h>
#if LV_COLOR_DEPTH != 16
#error "M1 LCD requires LVGL RGB565 (16-bit color)"
#endif
static esp_lcd_panel_handle_t panel;
#if !LV_COLOR_16_SWAP
static uint16_t *wire_pixels;
#endif
static lv_color_t *draw_pixels;
static bool display_asleep;
/* Values from the supplied TFT_eSPI ILI9341_2_DRIVER example.
 * Do not use the registry's init_cmds_2 header: it is a different sequence.
 * Direction and inversion are applied explicitly after initialization.
 */
static const ili9341_lcd_init_cmd_t m1_init[] = {
    {0xCF,(uint8_t[]){0x00,0xC1,0x30},3,0},
    {0xED,(uint8_t[]){0x64,0x03,0x12,0x81},4,0},
    {0xE8,(uint8_t[]){0x85,0x00,0x78},3,0},
    {0xCB,(uint8_t[]){0x39,0x2C,0x00,0x34,0x02},5,0},
    {0xF7,(uint8_t[]){0x20},1,0},
    {0xEA,(uint8_t[]){0x00,0x00},2,0},
    {0xC0,(uint8_t[]){0x10},1,0},
    {0xC1,(uint8_t[]){0x00},1,0},
    {0xC5,(uint8_t[]){0x30,0x30},2,0},
    {0xC7,(uint8_t[]){0xB7},1,0},
    {0xB1,(uint8_t[]){0x00,0x1A},2,0},
    {0xB6,(uint8_t[]){0x08,0x82,0x27},3,0},
    {0xF2,(uint8_t[]){0x00},1,0},
    {0x26,(uint8_t[]){0x01},1,0},
    {0xE0,(uint8_t[]){0x0F,0x2A,0x28,0x08,0x0E,0x08,0x54,0xA9,0x43,0x0A,0x0F,0x00,0x00,0x00,0x00},15,0},
    {0xE1,(uint8_t[]){0x00,0x15,0x17,0x07,0x11,0x06,0x2B,0x56,0x3C,0x05,0x10,0x0F,0x3F,0x3F,0x0F},15,0},
    {0x11,NULL,0,120},
};
static SemaphoreHandle_t transfer_done;
static lv_disp_draw_buf_t draw_buf;
static lv_disp_drv_t disp;
static lv_indev_drv_t touch;
static bool lcd_done(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *ev, void *ctx) {
    BaseType_t wake = pdFALSE;
    xSemaphoreGiveFromISR(transfer_done, &wake);
    return wake == pdTRUE;
}
static void flush(lv_disp_drv_t *d, const lv_area_t *a, lv_color_t *pixels) {
    size_t n = (a->x2-a->x1+1)*(a->y2-a->y1+1);
    configASSERT(n<=M1_LCD_WIDTH*M1_LCD_BUFFER_LINES);
    /* Never mutate LVGL's draw buffer. SPI sends high byte first.
     * LV_COLOR_16_SWAP may already have put LVGL pixels in wire order.
     */
#if LV_COLOR_16_SWAP
    const void *wire = pixels;
#else
    for (size_t i=0; i<n; ++i) wire_pixels[i]=__builtin_bswap16(pixels[i].full);
    const void *wire = wire_pixels;
#endif
    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel, a->x1,a->y1,a->x2+1,a->y2+1,wire));
    xSemaphoreTake(transfer_done, portMAX_DELAY);
    lv_disp_flush_ready(d);
}
/* Software SPI keeps SPI3 available for the SD slot's distinct wiring. */
static uint16_t sample(uint8_t command) {
    gpio_set_level(M1_TOUCH_CS,0);
    for(int i=7;i>=0;--i) {
        gpio_set_level(M1_TOUCH_MOSI,(command>>i)&1);
        gpio_set_level(M1_TOUCH_CLK,1); esp_rom_delay_us(2);
        gpio_set_level(M1_TOUCH_CLK,0); esp_rom_delay_us(2);
    }
    uint16_t value=0;
    for(int i=0;i<16;++i) {
        gpio_set_level(M1_TOUCH_CLK,1); esp_rom_delay_us(2);
        value=(value<<1)|gpio_get_level(M1_TOUCH_MISO);
        gpio_set_level(M1_TOUCH_CLK,0); esp_rom_delay_us(2);
    }
    gpio_set_level(M1_TOUCH_CS,1);
    return (value>>3)&4095;
}
static int map_axis(int v,int lo,int hi,int size) {
    int p=(v-lo)*(size-1)/(hi-lo);
    return p<0?0:p>=size?size-1:p;
}
static int median5(int values[5]) {
    for(int i=1;i<5;++i) {
        int v=values[i],j=i;
        while(j>0 && values[j-1]>v) {values[j]=values[j-1];--j;}
        values[j]=v;
    }
    return values[2];
}
static void read_touch(lv_indev_drv_t *d, lv_indev_data_t *data) {
    static bool held;
    static int last_x,last_y,candidate_x,candidate_y,press_count,weak_count;
    data->point.x=last_x;data->point.y=last_y;
    data->state=held?LV_INDEV_STATE_PR:LV_INDEV_STATE_REL;
    if(gpio_get_level(M1_TOUCH_IRQ)) {
        held=false;press_count=weak_count=0;data->state=LV_INDEV_STATE_REL;return;
    }
    /* Reference XPT2046 pressure estimate; filter noise before hit testing. */
    int z=sample(0xb0)+4095-sample(0xc0);
    if(z<200) {
        press_count=0;
        if(++weak_count>=2) {held=false;data->state=LV_INDEV_STATE_REL;}
        return;
    }
    weak_count=0;
    sample(0xd0);sample(0x90); /* Discard the first conversion after switching. */
    int xs[5],ys[5];
    for(int i=0;i<5;++i) {xs[i]=sample(0xd0);ys[i]=sample(0x90);}
    int x=median5(xs),y=median5(ys);
    if(M1_TOUCH_SWAP_XY) {int t=x;x=y;y=t;}
    if(x<M1_TOUCH_X_MIN-150 || x>M1_TOUCH_X_MAX+150 ||
       y<M1_TOUCH_Y_MIN-150 || y>M1_TOUCH_Y_MAX+150) {press_count=0;return;}
    x=map_axis(x,M1_TOUCH_X_MIN,M1_TOUCH_X_MAX,M1_LCD_WIDTH);
    y=map_axis(y,M1_TOUCH_Y_MIN,M1_TOUCH_Y_MAX,M1_LCD_HEIGHT);
    x=M1_TOUCH_INVERT_X?M1_LCD_WIDTH-1-x:x;
    y=M1_TOUCH_INVERT_Y?M1_LCD_HEIGHT-1-y:y;
    if(!held) {
        if(press_count==0 || abs(x-candidate_x)>12 || abs(y-candidate_y)>12) {
            candidate_x=x;candidate_y=y;press_count=1;return;
        }
        held=true;press_count=0;
    } else if(abs(x-last_x)>32 || abs(y-last_y)>32) {
        /* Reject sudden jumps to unrelated controls within one touch. */
        return;
    }
    last_x=x;last_y=y;data->point.x=x;data->point.y=y;
    data->state=LV_INDEV_STATE_PR;
}
static void tick(void *arg) {lv_tick_inc(2);}
static esp_err_t alloc_draw_buffers(void) {
    const size_t count=M1_LCD_WIDTH*M1_LCD_BUFFER_LINES;
    /* LVGL renders directly into DMA memory in LCD byte order. flush() waits
     * for SPI completion before this single buffer can be reused. */
    draw_pixels=heap_caps_malloc(count*sizeof(lv_color_t),MALLOC_CAP_DMA|MALLOC_CAP_INTERNAL);
    if(!draw_pixels) return ESP_ERR_NO_MEM;
#if !LV_COLOR_16_SWAP
    wire_pixels=heap_caps_malloc(count*sizeof(uint16_t),MALLOC_CAP_DMA|MALLOC_CAP_INTERNAL);
    if(!wire_pixels) {heap_caps_free(draw_pixels);draw_pixels=NULL;return ESP_ERR_NO_MEM;}
#endif
    lv_disp_draw_buf_init(&draw_buf,draw_pixels,NULL,count);
    return ESP_OK;
}
static void free_draw_buffers(void) {
    heap_caps_free(draw_pixels);draw_pixels=NULL;
#if !LV_COLOR_16_SWAP
    heap_caps_free(wire_pixels);wire_pixels=NULL;
#endif
}
esp_err_t board_display_init(void) {
    transfer_done=xSemaphoreCreateBinary();
    if(!transfer_done) return ESP_ERR_NO_MEM;
    spi_bus_config_t bus={.mosi_io_num=M1_LCD_MOSI,.miso_io_num=-1,.sclk_io_num=M1_LCD_CLK,
        .quadwp_io_num=-1,.quadhd_io_num=-1,.max_transfer_sz=M1_LCD_WIDTH*M1_LCD_BUFFER_LINES*2};
    ESP_RETURN_ON_ERROR(spi_bus_initialize(SPI2_HOST,&bus,SPI_DMA_CH_AUTO),"LCD","bus");
    esp_lcd_panel_io_handle_t io;
    esp_lcd_panel_io_spi_config_t cfg={.dc_gpio_num=M1_LCD_DC,.cs_gpio_num=M1_LCD_CS,
        .pclk_hz=M1_LCD_SPI_HZ,.spi_mode=0,.lcd_cmd_bits=8,.lcd_param_bits=8,.trans_queue_depth=1,
        .on_color_trans_done=lcd_done};
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST,&cfg,&io),"LCD","io");
    const ili9341_vendor_config_t vendor={.init_cmds=m1_init,.init_cmds_size=sizeof(m1_init)/sizeof(m1_init[0])};
    esp_lcd_panel_dev_config_t dev={.reset_gpio_num=-1,.rgb_ele_order=LCD_RGB_ELEMENT_ORDER_BGR,
        .bits_per_pixel=16,.vendor_config=(void *)&vendor};
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_ili9341(io,&dev,&panel),"LCD","panel");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(panel),"LCD","reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(panel),"LCD","init");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_swap_xy(panel,false),"LCD","portrait axes");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(panel,M1_LCD_MIRROR_X,M1_LCD_MIRROR_Y),"LCD","direction");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(panel,M1_LCD_INVERT_COLOR),"LCD","inversion");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(panel,true),"LCD","on");
    gpio_config_t out={.pin_bit_mask=(1ULL<<M1_LCD_BL)|(1ULL<<M1_TOUCH_CS)|(1ULL<<M1_TOUCH_CLK)|(1ULL<<M1_TOUCH_MOSI),.mode=GPIO_MODE_OUTPUT};
    ESP_RETURN_ON_ERROR(gpio_config(&out),"LCD","gpio");
    gpio_set_level(M1_LCD_BL,1); gpio_set_level(M1_TOUCH_CS,1);
    gpio_config_t in={.pin_bit_mask=(1ULL<<M1_TOUCH_IRQ)|(1ULL<<M1_TOUCH_MISO),.mode=GPIO_MODE_INPUT};
    ESP_RETURN_ON_ERROR(gpio_config(&in),"LCD","touch gpio");
    lv_init();
    ESP_RETURN_ON_ERROR(alloc_draw_buffers(),"LCD","draw buffer");
    lv_disp_drv_init(&disp); disp.hor_res=M1_LCD_WIDTH;disp.ver_res=M1_LCD_HEIGHT;disp.draw_buf=&draw_buf;disp.flush_cb=flush;
    if(!lv_disp_drv_register(&disp)) return ESP_ERR_NO_MEM;
    lv_indev_drv_init(&touch);touch.type=LV_INDEV_TYPE_POINTER;touch.read_cb=read_touch;
    if(!lv_indev_drv_register(&touch)) return ESP_ERR_NO_MEM;
    esp_timer_handle_t timer;
    esp_timer_create_args_t args={.callback=tick,.name="lv_tick"};
    ESP_RETURN_ON_ERROR(esp_timer_create(&args,&timer),"LCD","timer");
    return esp_timer_start_periodic(timer,2000);
}
/* Caller must not run lv_timer_handler() while asleep: the draw buffer is freed. */
esp_err_t board_display_sleep(void) {
    if(display_asleep) return ESP_OK;
    gpio_set_level(M1_LCD_BL,0);
    esp_lcd_panel_disp_on_off(panel,false);
    free_draw_buffers();
    display_asleep=true;
    return ESP_OK;
}
/* Restores the draw buffer and panel output. The backlight stays off until the
 * caller has rendered a fresh frame, so the stale panel image is not shown. */
esp_err_t board_display_wake(void) {
    if(!display_asleep) return ESP_OK;
    ESP_RETURN_ON_ERROR(alloc_draw_buffers(),"LCD","draw buffer");
    esp_lcd_panel_disp_on_off(panel,true);
    lv_obj_invalidate(lv_scr_act());
    display_asleep=false;
    return ESP_OK;
}
void board_display_backlight(bool on) {gpio_set_level(M1_LCD_BL,on?1:0);}
/* Same pressure gate as read_touch(): PENIRQ alone also drops on noise. */
bool board_display_touch_down(void) {
    if(gpio_get_level(M1_TOUCH_IRQ)) return false;
    return sample(0xb0)+4095-sample(0xc0)>=200;
}

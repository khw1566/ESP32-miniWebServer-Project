/* PC renderer: real M1 UI and LVGL 8.4, deterministic service fixtures. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "../../../mini-WEB (M1-ESP-IDF_ver)/main/onboard_ui.c"

static lv_color_t framebuffer[240*320];
static lv_disp_t *sim_display;
static unsigned sim_ticks;
static net_mode_t sim_mode=NET_MODE_STA_CONNECTED;
static bool sim_web=true;
const char *esp_err_to_name(esp_err_t value) {return value==ESP_OK?"ESP_OK":"SIM_ERROR";}
size_t heap_caps_get_total_size(unsigned caps) {(void)caps;return 278*1024;}
size_t heap_caps_get_free_size(unsigned caps) {(void)caps;return 94*1024;}
int64_t esp_timer_get_time(void) {return (int64_t)sim_ticks*1000;}
net_mode_t network_get_mode(void) {return sim_mode;}
int8_t network_get_rssi(void) {return -47;}
esp_err_t network_start_softap(void) {sim_mode=NET_MODE_AP_CAPTIVE;return ESP_OK;}
esp_err_t network_connect_sta(const char *s,const char *p) {(void)s;(void)p;return ESP_OK;}
esp_err_t network_save_credentials(const char *s,const char *p) {(void)s;(void)p;return ESP_OK;}
bool network_has_saved_credentials(char *s,size_t n,char *p,size_t m) {snprintf(s,n,"MiniWeb-Demo");if(m)p[0]=0;return true;}
esp_err_t network_get_ip_str(char *out,size_t n) {snprintf(out,n,"192.0.2.31");return ESP_OK;}
esp_err_t network_get_ip6_str(char *out,size_t n) {snprintf(out,n,"2001:db8::31");return ESP_OK;}
esp_err_t network_get_mdns_url(char *out,size_t n) {snprintf(out,n,"http://miniweb-demo.local/");return ESP_OK;}
bool network_mdns_is_ready(void) {return true;}
int network_scan_wifi(wifi_scan_item_t *items,int count) {(void)items;(void)count;return 0;}
bool network_router_saved(void) {return false;}
bool network_router_active(void) {return false;}
esp_err_t network_router_start(bool remember) {(void)remember;return ESP_OK;}
esp_err_t network_router_stop(bool remember) {(void)remember;return ESP_OK;}
const char *network_router_ssid(void) {return "MiniWeb-Demo";}
const char *network_router_ip(void) {return "192.0.2.1";}
esp_err_t network_router_password(char *out,size_t n) {if(n)out[0]=0;return ESP_OK;}
int network_router_clients(void) {return 0;}
esp_err_t cloud_relay_get_url(char *out,size_t n) {snprintf(out,n,"https://miniweb.example/");return ESP_OK;}
esp_err_t web_server_start(void) {sim_web=true;return ESP_OK;}
void web_server_stop(void) {sim_web=false;}
bool web_server_is_running(void) {return sim_web;}
esp_err_t dns_server_start(void) {return ESP_OK;}
void dns_server_stop(void) {}
void screen_saver_request(void) {}
bool screen_saver_active(void) {return false;}
bool screen_saver_service(bool enabled,screen_saver_teardown_t fn) {(void)enabled;(void)fn;return false;}
void screen_saver_reveal(void) {}
unsigned m1_sound_get_volume(void) {return 2;}
esp_err_t m1_sound_set_volume(unsigned level) {(void)level;return ESP_OK;}
void storage_capacity_read(bool sd,storage_capacity_t *out) {
 *out=(storage_capacity_t){.total=sd?1024ULL*1024*1024:4*1024*1024,.used=sd?20*1024*1024:320*1024,.reserved=sd?0:2800*1024,.error=ESP_OK};
}
void storage_view_read(bool sd,const char *folder,unsigned offset,storage_view_t *out) {
 (void)folder;storage_capacity_t capacity;storage_capacity_read(sd,&capacity);
 memset(out,0,sizeof(*out));out->total=capacity.total;out->used=capacity.used;out->reserved=capacity.reserved;out->error=ESP_OK;out->count=3;out->shown=3;out->offset=offset;
 const char *names[]={"index.html","media","about.html"};
 for(unsigned i=0;i<3;++i) {snprintf(out->entries[i].name,sizeof(out->entries[i].name),"%s",names[i]);out->entries[i].directory=i==1;out->entries[i].bytes=i==1?0:8192+i*2048;}
}
static void sim_flush(lv_disp_drv_t *drv,const lv_area_t *area,lv_color_t *pixels) {
 unsigned width=lv_area_get_width(area);
 for(int y=area->y1;y<=area->y2;++y) memcpy(framebuffer+y*240+area->x1,pixels+(y-area->y1)*width,width*sizeof(lv_color_t));
 lv_disp_flush_ready(drv);
}
static void settle(unsigned ms) {for(unsigned n=0;n<ms;n+=10) {sim_ticks+=10;lv_tick_inc(10);lv_timer_handler();}}
static void capture(const char *dir,const char *name) {
 settle(150);lv_obj_invalidate(lv_scr_act());lv_refr_now(sim_display);
 char path[1024];snprintf(path,sizeof(path),"%s/%s.rgb565",dir,name);FILE *f=fopen(path,"wb");if(!f){perror(path);exit(1);}
 for(unsigned i=0;i<240*320;++i) {
  uint16_t value=framebuffer[i].full;
#if LV_COLOR_16_SWAP
  value=(value>>8)|(value<<8);
#endif
  fputc(value&255,f);fputc(value>>8,f);
 }
 fclose(f);printf("captured %s\n",name);
}
int main(int argc,char **argv) {
 if(argc<2)return 2;
 lv_init();static lv_color_t buffer[240*40];static lv_disp_draw_buf_t draw;
 lv_disp_draw_buf_init(&draw,buffer,NULL,240*40);static lv_disp_drv_t driver;lv_disp_drv_init(&driver);
 driver.hor_res=240;driver.ver_res=320;driver.flush_cb=sim_flush;driver.draw_buf=&draw;sim_display=lv_disp_drv_register(&driver);
 lv_theme_t *theme=lv_theme_default_init(sim_display,lv_palette_main(LV_PALETTE_BLUE),lv_palette_main(LV_PALETTE_RED),false,&font_m1);lv_disp_set_theme(sim_display,theme);
 capacity_ready=true;storage_capacity_read(false,&home_flash);storage_capacity_read(true,&home_sd);server_running=true;
 home();capture(argv[1],"m1_home");
 setup();scan_count=3;scan_offset=0;
 const char *aps[]={"MiniWeb-Demo","Studio-Network","Workshop"};for(int i=0;i<3;++i){snprintf(scan_cache[i].ssid,33,"%s",aps[i]);scan_cache[i].rssi=-47-i*9;}
 render_scan();capture(argv[1],"m1_wifi");
 lv_textarea_set_text(ssid,"MiniWeb-Demo");m1_keyboard_open(ssid,"SSID");capture(argv[1],"m1_keyboard");
 storage_sd=false;storage_view_read(false,"",0,&storage_cache);storage_render(false);capture(argv[1],"m1_storage_flash");
 storage_sd=true;storage_view_read(true,"",0,&storage_cache);storage_render(false);capture(argv[1],"m1_storage_sd");
 show_access_qr(QR_LOCAL);capture(argv[1],"m1_qr_local");
 sim_mode=NET_MODE_AP_CAPTIVE;softap();capture(argv[1],"m1_softap");
 help_index=0;help();capture(argv[1],"m1_help");
 return 0;
}

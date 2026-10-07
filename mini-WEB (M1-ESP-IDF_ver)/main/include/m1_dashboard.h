#pragma once
#include "lvgl.h"
#include "storage_view.h"
enum { M1_ICON_SETTINGS,M1_ICON_FILES,M1_ICON_QR,M1_ICON_SERVER,M1_ICON_AP,M1_ICON_HELP };
lv_obj_t *m1_dashboard_button(lv_obj_t *parent,const char *caption,unsigned icon,lv_event_cb_t cb);
void m1_dashboard_create(lv_obj_t *parent,lv_event_cb_t wifi_cb,
                          lv_event_cb_t server_cb,lv_event_cb_t storage_cb);
void m1_dashboard_reset(void);
void m1_dashboard_update(bool wifi_connected,bool wifi_ap,int rssi,bool server,
                          uint64_t ram_total,uint64_t ram_free,
                          const storage_capacity_t *flash,const storage_capacity_t *sd);

#include "m1_dashboard.h"
#include "board_display.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define BLUE 0x277AC7
#define GREEN 0x398B63
#define AMBER 0xD29932
#define RED 0xC64545
#define MUTED 0xD7DEE8
static struct {
    lv_obj_t *wifi_icon,*wifi_value,*server_icon,*server_value;
    unsigned wifi_bars;
    uint32_t wifi_color;
    bool server_on;
    lv_obj_t *values[3],*used[3],*reserved[3],*free[3];
} dashboard;

static void place(lv_obj_t *obj,int x,int y,int w,int h) {
    lv_obj_set_pos(obj,x,y);lv_obj_set_size(obj,w,h);
}
static lv_obj_t *box(lv_obj_t *parent,int x,int y,int w,int h,uint32_t color,int radius) {
    lv_obj_t *obj=lv_obj_create(parent);place(obj,x,y,w,h);
    lv_obj_clear_flag(obj,LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_pad_all(obj,0,0);lv_obj_set_style_border_width(obj,0,0);
    lv_obj_set_style_radius(obj,radius,0);lv_obj_set_style_bg_color(obj,lv_color_hex(color),0);
    return obj;
}
static lv_obj_t *text(lv_obj_t *parent,const char *value,int x,int y,int w) {
    lv_obj_t *obj=lv_label_create(parent);lv_label_set_text(obj,value);place(obj,x,y,w,20);
    lv_label_set_long_mode(obj,LV_LABEL_LONG_DOT);
    return obj;
}
static void clickable(lv_obj_t *obj,lv_event_cb_t cb,void *data) {
    if(!cb) return;
    lv_obj_add_flag(obj,LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(obj,cb,LV_EVENT_CLICKED,data);
}
/* Decorative primitives are draw commands, not separately allocated widgets.
 * The containing card/button remains the full-size touch target. */
static void paint(lv_draw_ctx_t *ctx,const lv_area_t *origin,int x,int y,int w,int h,uint32_t color,int radius) {
    lv_draw_rect_dsc_t dsc;lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color=lv_color_hex(color);dsc.bg_opa=LV_OPA_COVER;dsc.radius=radius;
    lv_area_t area={origin->x1+x,origin->y1+y,origin->x1+x+w-1,origin->y1+y+h-1};
    lv_draw_rect(ctx,&dsc,&area);
}
static void device_icon(lv_draw_ctx_t *ctx,const lv_area_t *a,unsigned kind) {
    paint(ctx,a,4,3,16,17,BLUE,2);
    if(kind==2) {
        for(int i=0;i<3;++i) paint(ctx,a,6+i*4,4,2,6,AMBER,0);
        paint(ctx,a,6,13,12,3,0xFFFFFF,0);
    } else {
        for(int i=0;i<3;++i) {
            paint(ctx,a,1,5+i*5,3,2,BLUE,0);paint(ctx,a,20,5+i*5,3,2,BLUE,0);
        }
        paint(ctx,a,7,7,10,2,0xFFFFFF,0);
        paint(ctx,a,7,12,kind?6:10,2,0xFFFFFF,0);
    }
}
static void qr_corner(lv_draw_ctx_t *ctx,const lv_area_t *a,int x,int y) {
    paint(ctx,a,x,y,7,7,BLUE,0);paint(ctx,a,x+1,y+1,5,5,0xF1F5FA,0);paint(ctx,a,x+2,y+2,3,3,BLUE,0);
}
static void draw_icon(lv_event_t *e) {
    unsigned icon=(unsigned)(uintptr_t)lv_event_get_user_data(e);
    lv_draw_ctx_t *ctx=lv_event_get_draw_ctx(e);lv_area_t a;
    lv_obj_get_coords(lv_event_get_target(e),&a);
    if(icon>=100) {device_icon(ctx,&a,icon-100);return;}
    a.x1+=6;a.y1+=6;
    if(icon==M1_ICON_SETTINGS) {
        paint(ctx,&a,8,0,4,20,BLUE,1);paint(ctx,&a,0,8,20,4,BLUE,1);
        paint(ctx,&a,3,3,14,14,BLUE,4);paint(ctx,&a,7,7,6,6,0xF1F5FA,3);
    } else if(icon==M1_ICON_FILES) device_icon(ctx,&a,2);
    else if(icon==M1_ICON_QR) {
        qr_corner(ctx,&a,0,0);qr_corner(ctx,&a,13,0);qr_corner(ctx,&a,0,13);
        paint(ctx,&a,10,10,4,4,BLUE,0);paint(ctx,&a,16,13,4,7,BLUE,0);paint(ctx,&a,10,17,3,3,BLUE,0);
    } else if(icon==M1_ICON_SERVER) {
        for(int i=0;i<3;++i) {paint(ctx,&a,0,i*7,20,5,BLUE,1);paint(ctx,&a,3,i*7+1,2,2,0xF1F5FA,0);}
    } else if(icon==M1_ICON_AP) {
        for(int i=0;i<4;++i) paint(ctx,&a,i*6,20-(i+1)*5,4,(i+1)*5,BLUE,1);
    } else {
        paint(ctx,&a,0,0,20,20,BLUE,10);
        lv_draw_label_dsc_t dsc;lv_draw_label_dsc_init(&dsc);
        dsc.font=&lv_font_montserrat_14;dsc.color=lv_color_white();dsc.align=LV_TEXT_ALIGN_CENTER;
        a.x2=a.x1+19;a.y2=a.y1+19;lv_draw_label(ctx,&dsc,&a,"?",NULL);
    }
}
static void draw_status(lv_event_t *e) {
    lv_draw_ctx_t *ctx=lv_event_get_draw_ctx(e);lv_area_t a;
    lv_obj_get_coords(lv_event_get_target(e),&a);
    if(lv_event_get_user_data(e)) {
        for(int i=0;i<3;++i) {
            paint(ctx,&a,8,7+i*9,26,7,0x536579,2);
            paint(ctx,&a,11,9+i*9,3,3,dashboard.server_on?GREEN:RED,1);
            paint(ctx,&a,19,9+i*9,11,2,0xFFFFFF,0);
        }
    } else for(unsigned i=0;i<4;++i)
        paint(ctx,&a,8+i*8,30-(i+1)*5,5,(i+1)*5,i<dashboard.wifi_bars?dashboard.wifi_color:MUTED,1);
}
lv_obj_t *m1_dashboard_button(lv_obj_t *parent,const char *caption,unsigned icon,lv_event_cb_t cb) {
    lv_obj_t *button=lv_btn_create(parent);
    lv_obj_set_style_pad_all(button,0,0);
    lv_obj_set_style_bg_color(button,lv_color_hex(0xF1F5FA),0);
    lv_obj_set_style_text_color(button,lv_color_hex(0x243244),0);
    lv_obj_add_event_cb(button,cb,LV_EVENT_CLICKED,NULL);
    lv_obj_add_event_cb(button,draw_icon,LV_EVENT_DRAW_MAIN,(void *)(uintptr_t)icon);
    text(button,caption,32,6,76);
    return button;
}
void m1_dashboard_reset(void) {memset(&dashboard,0,sizeof(dashboard));}
void m1_dashboard_create(lv_obj_t *parent,lv_event_cb_t wifi_cb,
                          lv_event_cb_t server_cb,lv_event_cb_t storage_cb) {
    m1_dashboard_reset();
    lv_obj_t *wifi=box(parent,4,0,112,42,0xF1F5FA,5);clickable(wifi,wifi_cb,NULL);
    dashboard.wifi_icon=wifi;lv_obj_add_event_cb(wifi,draw_status,LV_EVENT_DRAW_MAIN,NULL);
    text(wifi,"Wi-Fi",45,1,64);dashboard.wifi_value=text(wifi,"OFF",45,21,64);
    lv_obj_set_style_text_font(dashboard.wifi_value,&lv_font_montserrat_14,0);
    lv_obj_t *server=box(parent,124,0,112,42,0xF1F5FA,5);clickable(server,server_cb,NULL);
    dashboard.server_icon=server;lv_obj_add_event_cb(server,draw_status,LV_EVENT_DRAW_MAIN,(void *)1);
    text(server,"WEB",45,1,64);dashboard.server_value=text(server,"OFF",45,21,64);
    lv_obj_set_style_text_font(dashboard.server_value,&lv_font_montserrat_14,0);
    const char *names[]={"RAM","FLASH","SD"};
    for(unsigned i=0;i<3;++i) {
        lv_obj_t *row=box(parent,4,48+(int)i*36,232,32,0xF1F5FA,4);
        if(i) clickable(row,storage_cb,(void *)(uintptr_t)(i==2));
        lv_obj_add_event_cb(row,draw_icon,LV_EVENT_DRAW_MAIN,(void *)(uintptr_t)(100+i));
        lv_obj_t *caption=text(row,names[i],28,0,48);
        lv_obj_set_style_text_font(caption,&lv_font_montserrat_14,0);
        dashboard.values[i]=text(row,"조회 중",78,0,150);
        dashboard.free[i]=box(row,28,22,196,8,GREEN,2);
        dashboard.reserved[i]=box(row,28,22,1,8,AMBER,0);
        dashboard.used[i]=box(row,28,22,1,8,BLUE,0);
        lv_obj_add_flag(dashboard.reserved[i],LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(dashboard.used[i],LV_OBJ_FLAG_HIDDEN);
    }
}
static void compact_size(char *out,size_t size,uint64_t bytes) {
    if(bytes>=1024ULL*1024*1024) snprintf(out,size,"%.1fG",(double)bytes/(1024*1024*1024));
    else if(bytes>=1024*1024) snprintf(out,size,"%.1fM",(double)bytes/(1024*1024));
    else if(bytes>=1024) snprintf(out,size,"%.0fK",(double)bytes/1024);
    else snprintf(out,size,"%lluB",(unsigned long long)bytes);
}
static void capacity(unsigned row,const storage_capacity_t *info) {
    lv_obj_t *value=dashboard.values[row];
    if(!info||info->error!=ESP_OK||!info->total) {
        lv_label_set_text(value,!info?"조회 중":row==2&&info->error==ESP_ERR_INVALID_STATE?"미인식":"조회 실패");
        lv_obj_set_style_text_font(value,&font_m1,0);
        lv_obj_add_flag(dashboard.used[row],LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(dashboard.reserved[row],LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_bg_color(dashboard.free[row],lv_color_hex(MUTED),0);
        return;
    }
    uint64_t reserved=info->reserved>info->total?info->total:info->reserved;
    uint64_t used=info->used>info->total-reserved?info->total-reserved:info->used;
    uint64_t occupied=used+reserved;
    char used_text[20],total_text[20];compact_size(used_text,sizeof(used_text),occupied);compact_size(total_text,sizeof(total_text),info->total);
    lv_label_set_text_fmt(value,"%u%% %s/%s",(unsigned)((double)occupied/info->total*100),used_text,total_text);
    lv_obj_set_style_text_font(value,&lv_font_montserrat_14,0);
    /* Chart widths use occupied totals to keep rounded segments within 196 px. */
    int reserved_width=(int)((double)reserved/info->total*196);
    int occupied_width=(int)((double)occupied/info->total*196);
    int used_width=occupied_width-reserved_width;
    lv_obj_set_style_bg_color(dashboard.free[row],lv_color_hex(GREEN),0);
    if(reserved_width) {lv_obj_clear_flag(dashboard.reserved[row],LV_OBJ_FLAG_HIDDEN);place(dashboard.reserved[row],28,22,reserved_width,8);}
    else lv_obj_add_flag(dashboard.reserved[row],LV_OBJ_FLAG_HIDDEN);
    if(used_width) {lv_obj_clear_flag(dashboard.used[row],LV_OBJ_FLAG_HIDDEN);place(dashboard.used[row],28+reserved_width,22,used_width,8);}
    else lv_obj_add_flag(dashboard.used[row],LV_OBJ_FLAG_HIDDEN);
}
void m1_dashboard_update(bool connected,bool ap,int rssi,bool server,
                          uint64_t ram_total,uint64_t ram_free,
                          const storage_capacity_t *flash,const storage_capacity_t *sd) {
    if(!dashboard.wifi_value) return;
    unsigned bars=connected?(rssi==0?0:rssi>=-60?4:rssi>=-70?3:rssi>=-80?2:1):ap?4:0;
    uint32_t color=ap?BLUE:bars>=3?GREEN:AMBER;
    if(dashboard.wifi_bars!=bars || dashboard.wifi_color!=color) {
        dashboard.wifi_bars=bars;dashboard.wifi_color=color;lv_obj_invalidate(dashboard.wifi_icon);
    }
    if(connected&&rssi!=0) lv_label_set_text_fmt(dashboard.wifi_value,"%d dBm",rssi);
    else if(connected) lv_label_set_text(dashboard.wifi_value,"ON");
    else lv_label_set_text(dashboard.wifi_value,ap?"AP":"OFF");
    lv_label_set_text(dashboard.server_value,server?"ON":"OFF");
    if(dashboard.server_on!=server) {dashboard.server_on=server;lv_obj_invalidate(dashboard.server_icon);}
    storage_capacity_t ram={.total=ram_total,.used=ram_total>=ram_free?ram_total-ram_free:0,.error=ESP_OK};
    capacity(0,&ram);capacity(1,flash);capacity(2,sd);
}

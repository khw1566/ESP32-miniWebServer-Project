#include "board_display.h"
#include "network_manager.h"
#include "web_server.h"
#include "cloud_relay.h"
#include "storage_view.h"
#include "m1_keyboard.h"
#include "m1_dashboard.h"
#include "esp_heap_caps.h"
#include "qrcode.h"
#include "dns_server.h"
#include "esp_wifi.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_spiffs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdatomic.h>

typedef struct {int op;char ssid[33];char pass[65];bool sd;unsigned offset;char folder[256];} command_t;
typedef struct {int op;esp_err_t err;int count;wifi_scan_item_t aps[12];storage_view_t storage;storage_capacity_t flash,sd;} result_t;
static QueueHandle_t commands,results;
static lv_obj_t *body,*status,*ssid,*password,*scan_list;
static lv_obj_t *pager,*page_label;
static lv_obj_t *setup_actions[3];
static wifi_scan_item_t scan_cache[12];
static int scan_count,scan_offset;
static bool busy;
static atomic_bool server_running;
static atomic_bool auto_server_requested;
static int page;
static int help_index;
static const char *pending_save_message;
static void home(void);
static void storage_request(void);
static void storage_render(bool loading);
static void storage_list_cb(lv_event_t *e);
static bool storage_sd;
static unsigned storage_offset;
static char storage_folder[256];
static storage_view_t storage_cache;
static storage_capacity_t home_flash,home_sd;
static bool capacity_pending,capacity_ready;
static uint8_t *qr_pixels;
static void setup(void);
static void softap(void);
static void help(void);
static void render_scan(void);
/* All pages reserve 28 px for the title and 24 px for status.
 * Content and actions must fit inside the remaining 264 px without scrolling.
 */
static void rect(lv_obj_t *obj,int x,int y,int w,int h) {
    lv_obj_set_pos(obj,x,y);lv_obj_set_size(obj,w,h);
}
static void fixed_container(lv_obj_t *obj) {
    lv_obj_clear_flag(obj,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(obj,0,0);
    lv_obj_set_style_border_width(obj,0,0);
    lv_obj_set_style_radius(obj,0,0);
}
static void worker(void *arg) {
    command_t cmd;
    while(xQueueReceive(commands,&cmd,portMAX_DELAY)) {
        result_t r={.op=cmd.op,.err=ESP_OK};
        if(cmd.op==1) {
            r.count=network_scan_wifi(r.aps,12);
            if(r.count<0) {r.err=-r.count;r.count=0;}
        } else if(cmd.op==2) {
            auto_server_requested=true;
            dns_server_stop();
            if(server_running) {web_server_stop();server_running=false;}
            r.err=network_connect_sta(cmd.ssid,cmd.pass);
            if(r.err==ESP_OK) {
                r.err=network_save_credentials(cmd.ssid,cmd.pass);
                /* A persistence failure does not invalidate a working connection. */
                esp_err_t start=web_server_start();
                server_running=start==ESP_OK;
                if(start!=ESP_OK) r.err=start;
            }
        } else if(cmd.op==3) {
            auto_server_requested=!server_running;
            if(server_running) {web_server_stop();server_running=false;}
            else {r.err=web_server_start();server_running=r.err==ESP_OK;}
        } else if(cmd.op==4) {
            auto_server_requested=false;
            dns_server_stop();
            if(server_running) {web_server_stop();server_running=false;}
            r.err=network_start_softap();
            if(r.err==ESP_OK) r.err=dns_server_start();
            if(r.err==ESP_OK) {
                r.err=web_server_start();server_running=r.err==ESP_OK;
            }
            if(r.err!=ESP_OK) dns_server_stop();
        } else if(cmd.op==5) {
            storage_view_read(cmd.sd,cmd.folder,cmd.offset,&r.storage);
            r.err=r.storage.error;
        } else if(cmd.op==6) {
            storage_capacity_read(false,&r.flash);
            storage_capacity_read(true,&r.sd);
        }
        memset(cmd.pass,0,sizeof(cmd.pass));
        xQueueSend(results,&r,portMAX_DELAY);
    }
}
static lv_obj_t *label(lv_obj_t *parent,const char *text) {
    lv_obj_t *l=lv_label_create(parent);lv_label_set_text(l,text);
    lv_obj_set_width(l,LV_PCT(100));lv_label_set_long_mode(l,LV_LABEL_LONG_DOT);return l;
}
static lv_obj_t *button(lv_obj_t *parent,const char *text,lv_event_cb_t cb,void *data) {
    lv_obj_t *b=lv_btn_create(parent);lv_obj_set_width(b,LV_PCT(100));
    lv_obj_set_height(b,36);lv_obj_add_event_cb(b,cb,LV_EVENT_CLICKED,data);
    lv_obj_t *l=lv_label_create(b);lv_label_set_text(l,text);lv_obj_center(l);return b;
}
static void submit(int op) {
    if(busy) return;
    if(op==3 && !server_running && network_get_mode()!=NET_MODE_STA_CONNECTED && network_get_mode()!=NET_MODE_AP_CAPTIVE) {
        lv_label_set_text(status,"Wi-Fi 연결 필요");return;
    }
    command_t c={.op=op};
    if(op==2) {
        if(strlen(lv_textarea_get_text(ssid))>32 || strlen(lv_textarea_get_text(password))>63) {
            lv_label_set_text(status,"입력 길이 초과");return;
        }
        snprintf(c.ssid,sizeof(c.ssid),"%s",lv_textarea_get_text(ssid));
        snprintf(c.pass,sizeof(c.pass),"%s",lv_textarea_get_text(password));
        size_t n=strlen(c.pass);
        if(!c.ssid[0] || (n && (n<8 || n>63))) {
            lv_label_set_text(status,"SSID / 비밀번호 확인 (8~63)");return;
        }
        m1_keyboard_close();
    }
    if(xQueueSend(commands,&c,0)==pdTRUE) {
        busy=true;lv_label_set_text(status,op==1?"검색 중":"처리 중");
    }
    memset(c.pass,0,sizeof(c.pass));
}
static void scan_cb(lv_event_t *e) {submit(1);}
static void connect_cb(lv_event_t *e) {submit(2);}
static void toggle_cb(lv_event_t *e) {submit(3);}
static void setup_cb(lv_event_t *e) {if(!busy) setup();}
static void home_cb(lv_event_t *e) {if(!busy && !m1_keyboard_is_open()) home();}
static void softap_cb(lv_event_t *e) {if(!busy) softap();}
static void ap_start_cb(lv_event_t *e) {submit(4);}
static void help_cb(lv_event_t *e) {if(!busy) {help_index=0;help();}}
static void pick_cb(lv_event_t *e) {
    int index=(int)(intptr_t)lv_event_get_user_data(e);
    if(index<0 || index>=scan_count || busy) return;
    lv_textarea_set_text(ssid,scan_cache[index].ssid);
    lv_textarea_set_text(password,"");
    m1_keyboard_open(password,"비밀번호");
}
static void focus_cb(lv_event_t *e) {
    if(busy) return;
    lv_obj_t *target=lv_event_get_target(e);
    m1_keyboard_open(target,target==ssid?"SSID":"비밀번호");
}
static void clear_page(const char *title) {
    m1_keyboard_reset();
    m1_dashboard_reset();
    lv_obj_clean(lv_scr_act());
    /* The canvas is deleted before its backing pixels are released. */
    free(qr_pixels); qr_pixels=NULL;
    fixed_container(lv_scr_act());
    lv_obj_set_style_text_font(lv_scr_act(),&font_m1,0);
    lv_obj_set_style_text_line_space(lv_scr_act(),0,0);
    lv_obj_t *heading=lv_label_create(lv_scr_act());
    lv_label_set_text(heading,title);lv_obj_align(heading,LV_ALIGN_TOP_MID,0,5);
    body=lv_obj_create(lv_scr_act());rect(body,0,28,240,264);fixed_container(body);
    status=lv_label_create(lv_scr_act());lv_label_set_text(status,"");
    rect(status,4,296,232,20);lv_label_set_long_mode(status,LV_LABEL_LONG_DOT);
}
static void render_scan(void) {
    lv_obj_clean(scan_list);
    for(int row=0;row<3 && scan_offset+row<scan_count;++row) {
        int index=scan_offset+row;
        lv_obj_t *b=button(scan_list,scan_cache[index].ssid,pick_cb,(void *)(intptr_t)index);
        rect(b,0,row*38,232,34);
        lv_obj_set_style_pad_all(b,4,0);
        lv_obj_t *l=lv_obj_get_child(b,0);lv_obj_set_width(l,220);
        lv_label_set_long_mode(l,LV_LABEL_LONG_DOT);lv_obj_center(l);
    }
    if(!scan_count) {lv_obj_t *l=label(scan_list,"검색 후 목록에서 선택");rect(l,0,32,232,20);}
    lv_label_set_text_fmt(page_label,"%d / %d",scan_count?scan_offset/3+1:0,(scan_count+2)/3);
    lv_obj_t *prev=lv_obj_get_child(pager,0),*next=lv_obj_get_child(pager,1);
    if(scan_offset==0) lv_obj_add_state(prev,LV_STATE_DISABLED);else lv_obj_clear_state(prev,LV_STATE_DISABLED);
    if(scan_offset+3>=scan_count) lv_obj_add_state(next,LV_STATE_DISABLED);else lv_obj_clear_state(next,LV_STATE_DISABLED);
}
static void page_cb(lv_event_t *e) {
    int next=scan_offset+(int)(intptr_t)lv_event_get_user_data(e);
    if(busy || next<0 || next>=scan_count) return;
    scan_offset=next;render_scan();
}
static void setup(void) {
    page=1;clear_page("Wi-Fi 설정");
    setup_actions[0]=button(body,"검색",scan_cb,NULL);rect(setup_actions[0],4,0,74,32);
    setup_actions[1]=button(body,"연결",connect_cb,NULL);rect(setup_actions[1],83,0,74,32);
    setup_actions[2]=button(body,"홈",home_cb,NULL);rect(setup_actions[2],162,0,74,32);
    ssid=lv_textarea_create(body);lv_textarea_set_one_line(ssid,true);
    lv_textarea_set_max_length(ssid,32);rect(ssid,4,36,232,32);
    lv_textarea_set_placeholder_text(ssid,"SSID");lv_obj_set_style_pad_all(ssid,4,0);
    password=lv_textarea_create(body);lv_textarea_set_one_line(password,true);
    lv_textarea_set_max_length(password,63);lv_textarea_set_password_mode(password,false);
    rect(password,4,72,232,32);lv_obj_set_style_pad_all(password,4,0);
    lv_textarea_set_placeholder_text(password,"비밀번호 (개방형: 비움)");
    scan_list=lv_obj_create(body);rect(scan_list,4,112,232,114);fixed_container(scan_list);
    pager=lv_obj_create(body);rect(pager,4,232,232,32);fixed_container(pager);
    rect(button(pager,"이전",page_cb,(void *)(intptr_t)-3),0,0,68,32);
    rect(button(pager,"다음",page_cb,(void *)(intptr_t)3),164,0,68,32);
    page_label=label(pager,"");rect(page_label,74,6,84,20);
    lv_obj_set_style_text_align(page_label,LV_TEXT_ALIGN_CENTER,0);
    render_scan();
    m1_keyboard_create(body,status,scan_list,pager,setup_actions);
    lv_obj_add_event_cb(ssid,focus_cb,LV_EVENT_CLICKED,NULL);
    lv_obj_add_event_cb(password,focus_cb,LV_EVENT_CLICKED,NULL);
    lv_label_set_text(status,"");
}
static void show_qr(int kind);
static void ap_wifi_qr_cb(lv_event_t *e) {if(!busy) show_qr(1);}
static void ap_url_qr_cb(lv_event_t *e) {if(!busy) show_qr(2);}
static void show_qr(int kind) {
    bool external=kind==0 && cloud_relay_is_online();
    page=kind?kind+3:2;clear_page(kind==1?"SoftAP 연결 QR":kind==2?"SoftAP 설정 QR":external?"외부 접속 QR":"로컬 접속 QR");
    char ip[32]={0},url[96];
    net_mode_t mode=network_get_mode();
    if(!server_running || (kind?mode!=NET_MODE_AP_CAPTIVE:mode!=NET_MODE_STA_CONNECTED)) {
        rect(label(body,"Wi-Fi / 서버 실행 필요"),4,24,232,20);
        rect(button(body,"홈",home_cb,NULL),4,232,232,32);return;
    }
    if(kind==1) snprintf(url,sizeof(url),"WIFI:T:nopass;S:%s;;",SOFTAP_SSID_DEFAULT);
    else if(kind==2) snprintf(url,sizeof(url),"http://192.168.4.1/setup");
    else if(external && cloud_relay_get_url(url,sizeof(url))==ESP_OK) {}
    else {external=false;network_get_ip_str(ip,sizeof(ip));snprintf(url,sizeof(url),"http://%s/",ip);}
    QRCode code;uint8_t bits[512];
    if(qrcode_getBufferSize(4)>sizeof(bits) || qrcode_initText(&code,bits,4,ECC_MEDIUM,url)<0) {
        rect(label(body,"QR 생성 실패"),4,24,232,20);
        rect(button(body,"홈",home_cb,NULL),4,232,232,32);return;
    }
    /* 4-module white quiet zone, integer scaling, no interpolation. */
    qr_pixels=malloc(LV_CANVAS_BUF_SIZE_INDEXED_1BIT(180,180));
    if(!qr_pixels) {
        rect(label(body,"QR 생성 실패"),4,24,232,20);
        rect(button(body,"홈",home_cb,NULL),4,232,232,32);return;
    }
    lv_obj_t *canvas=lv_canvas_create(body);
    lv_canvas_set_buffer(canvas,qr_pixels,180,180,LV_IMG_CF_INDEXED_1BIT);
    lv_obj_set_pos(canvas,30,0);
    lv_canvas_set_palette(canvas,0,lv_color_white());
    lv_canvas_set_palette(canvas,1,lv_color_black());
    lv_color_t white={.full=0},black={.full=1};
    lv_canvas_fill_bg(canvas,white,LV_OPA_COVER);
    for(int y=0;y<code.size;++y) for(int x=0;x<code.size;++x) if(qrcode_getModule(&code,x,y)) {
        for(int dy=0;dy<4;++dy) for(int dx=0;dx<4;++dx)
            lv_canvas_set_px_color(canvas,8+(x+4)*4+dx,8+(y+4)*4+dy,black);
    }
    lv_obj_t *qr_url=label(body,kind==1?SOFTAP_SSID_DEFAULT:url);
    rect(qr_url,4,184,232,20);lv_label_set_long_mode(qr_url,LV_LABEL_LONG_DOT);
    rect(label(body,kind==1?"스캔 후 Wi-Fi 가입 허용":kind==2?"SoftAP 연결 후 열기":external?"인터넷에서 접속 가능":"같은 Wi-Fi에서 접속"),4,206,232,20);
    if(kind) {
        rect(button(body,kind==1?"설정 QR":"연결 QR",kind==1?ap_url_qr_cb:ap_wifi_qr_cb,NULL),4,232,112,32);
        rect(button(body,"뒤로",softap_cb,NULL),124,232,112,32);
    } else rect(button(body,"홈",home_cb,NULL),4,232,232,32);
}
static void qr_cb(lv_event_t *e) {if(!busy) show_qr(network_get_mode()==NET_MODE_AP_CAPTIVE?2:0);}
static void softap(void) {
    page=3;clear_page("SoftAP 설정");
    bool active=network_get_mode()==NET_MODE_AP_CAPTIVE && server_running;
    char text[256];
    snprintf(text,sizeof(text),"상태: %s\nSSID: %s\n비밀번호: 없음\n주소: 192.168.4.1/setup\n\n1. 연결 QR로 Wi-Fi 가입\n2. 설정 QR로 페이지 열기",
             active?"실행":"정지",SOFTAP_SSID_DEFAULT);
    rect(label(body,text),4,0,232,144);
    rect(button(body,active?"SoftAP 다시 시작":"SoftAP 시작",ap_start_cb,NULL),4,154,232,32);
    lv_obj_t *join=button(body,"연결 QR",ap_wifi_qr_cb,NULL),*portal=button(body,"설정 QR",ap_url_qr_cb,NULL);
    rect(join,4,192,112,32);rect(portal,124,192,112,32);
    if(!active) {lv_obj_add_state(join,LV_STATE_DISABLED);lv_obj_add_state(portal,LV_STATE_DISABLED);}
    rect(button(body,"사용법",help_cb,NULL),4,232,112,32);
    rect(button(body,"홈",home_cb,NULL),124,232,112,32);
}
static const char *help_text[]={
    "1. 연결 방법 선택\n보드 설정: 화면에서 입력\nSoftAP: 휴대폰에서 설정\n\n홈에서 SoftAP 선택 후\n시작 버튼을 누르세요.\n기존 공유기 연결은\nSoftAP로 전환됩니다.\n저장된 설정은 유지됩니다.",
    "2. 휴대폰 Wi-Fi 연결\n연결 QR을 카메라로 읽고\nWi-Fi 가입을 허용하세요.\nSSID:\nMini-WebServer-Setup\n비밀번호는 없습니다.\n인터넷 없음 안내에서도\n이 Wi-Fi 연결을 유지하세요.\n가입이 안 되면 직접 선택.",
    "3. 공유기 정보 입력\n설정 QR로 페이지를 여세요.\n안 열리면 브라우저에서\n192.168.4.1/setup 입력.\n공유기 SSID를 선택하고\n비밀번호를 입력하세요.\n저장하면 보드가 재시작.\n연결 실패 시 홈에서\nSoftAP를 다시 시작하세요.",
    "4. 연결 완료 후 접속\n외부 연결을 기다리세요.\n홈에서 외부: 연결 확인.\n접속 QR을 열고\n카메라로 읽으세요.\n휴대폰 LTE도 사용 가능.\n외부 연결 전에는 로컬 QR.\n로컬은 같은 Wi-Fi 필요.\n이후 부팅은 자동 연결.\n보드 설정도 계속 사용 가능."
};
static void help_step_cb(lv_event_t *e) {
    if(busy) return;
    help_index=(help_index+1)%4;help();
}
static void help(void) {
    page=6;char title[32];snprintf(title,sizeof(title),"사용법 %d / 4",help_index+1);clear_page(title);
    rect(label(body,help_text[help_index]),4,0,232,220);
    rect(button(body,help_index==3?"처음":"다음",help_step_cb,NULL),4,232,112,32);
    rect(button(body,"홈",home_cb,NULL),124,232,112,32);
}
static void storage_size(char *out,size_t length,uint64_t bytes) {
    if(bytes>=1024ULL*1024*1024) snprintf(out,length,"%.1f GB",(double)bytes/(1024*1024*1024));
    else if(bytes>=1024*1024) snprintf(out,length,"%.1f MB",(double)bytes/(1024*1024));
    else if(bytes>=1024) snprintf(out,length,"%.1f KB",(double)bytes/1024);
    else snprintf(out,length,"%llu B",(unsigned long long)bytes);
}
static void storage_choose_cb(lv_event_t *e) {
    if(busy) return;
    storage_sd=(intptr_t)lv_event_get_user_data(e)!=0;
    storage_offset=0;storage_folder[0]=0;storage_request();
}
static void storage_open_cb(lv_event_t *e) {
    if(busy) return;
    unsigned index=(unsigned)(uintptr_t)lv_event_get_user_data(e);
    if(index>=storage_cache.shown) return;
    storage_view_entry_t *entry=&storage_cache.entries[index];
    if(entry->directory) {
        char folder[256];
        size_t parent_length=strlen(storage_folder),name_length=strlen(entry->name);
        if(parent_length+name_length+2>sizeof(folder)) {lv_label_set_text(status,"폴더 경로가 너무 깁니다");return;}
        memcpy(folder,storage_folder,parent_length);folder[parent_length]='/';
        memcpy(folder+parent_length+1,entry->name,name_length+1);
        memcpy(storage_folder,folder,parent_length+name_length+2);storage_offset=0;storage_request();return;
    }
    page=8;clear_page("파일 정보");
    lv_obj_t *name=label(body,entry->name);rect(name,4,12,232,24);
    lv_label_set_long_mode(name,LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_t *folder=label(body,storage_folder[0]?storage_folder:"/");rect(folder,4,50,232,24);
    lv_label_set_long_mode(folder,LV_LABEL_LONG_SCROLL_CIRCULAR);
    char bytes[24],text[80];storage_size(bytes,sizeof(bytes),entry->bytes);
    snprintf(text,sizeof(text),"%s\n용량: %s\n%llu bytes",storage_sd?"SD카드":"내부 플래시",bytes,(unsigned long long)entry->bytes);
    rect(label(body,text),4,88,232,72);
    rect(label(body,"긴 파일명은 옆으로 흐릅니다"),4,176,232,24);
    rect(button(body,"목록",storage_list_cb,NULL),4,232,112,32);
    rect(button(body,"홈",home_cb,NULL),124,232,112,32);
}
static void storage_list_cb(lv_event_t *e) {if(!busy) storage_request();}
static void storage_step_cb(lv_event_t *e) {
    if(busy) return;
    int step=(int)(intptr_t)lv_event_get_user_data(e);
    if(step<0) {if(storage_offset<STORAGE_VIEW_ROWS) return;storage_offset-=STORAGE_VIEW_ROWS;}
    else {if(storage_offset+STORAGE_VIEW_ROWS>=storage_cache.count) return;storage_offset+=STORAGE_VIEW_ROWS;}
    storage_request();
}
static void storage_parent_cb(lv_event_t *e) {
    if(busy) return;
    char *slash=strrchr(storage_folder,'/');if(slash) *slash=0;
    storage_offset=0;storage_request();
}
static void storage_home_cb(lv_event_t *e) {
    if(busy) return;
    storage_sd=false;storage_folder[0]=0;storage_offset=0;storage_request();
}
static void storage_arc(unsigned start,unsigned end,uint32_t color,bool background) {
    lv_obj_t *arc=lv_arc_create(body);rect(arc,4,38,84,84);
    lv_obj_set_style_pad_all(arc,0,0);
    lv_obj_clear_flag(arc,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_style(arc,NULL,LV_PART_KNOB);
    lv_arc_set_rotation(arc,270);lv_arc_set_bg_angles(arc,0,360);lv_arc_set_angles(arc,start,end);
    lv_obj_set_style_arc_width(arc,12,LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc,lv_color_hex(0x398B63),LV_PART_MAIN);
    lv_obj_set_style_arc_opa(arc,background?LV_OPA_COVER:LV_OPA_TRANSP,LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc,12,LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc,lv_color_hex(color),LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(arc,false,LV_PART_INDICATOR);
}
static void storage_render(bool loading) {
    page=7;clear_page("저장 공간");
    rect(button(body,"내부 플래시",storage_choose_cb,(void *)0),4,0,112,30);
    rect(button(body,"SD카드",storage_choose_cb,(void *)1),124,0,112,30);
    if(loading) {
        rect(label(body,"저장 공간 조회 중"),4,72,232,24);
    } else if(storage_cache.error!=ESP_OK) {
        rect(label(body,storage_sd?"SD카드 조회 불가":"플래시 조회 불가"),4,60,232,24);
        rect(label(body,storage_sd?"카드 삽입 후 재부팅하세요":"저장 공간 초기화를 확인하세요"),4,96,232,48);
        lv_label_set_text_fmt(status,"%s",esp_err_to_name(storage_cache.error));
    } else {
        uint64_t total=storage_cache.total,used=storage_cache.used,reserved=storage_cache.reserved;
        uint64_t occupied=used+reserved;
        if(occupied>total) occupied=total;
        uint64_t free_bytes=total-occupied;
        unsigned used_angle=total?(unsigned)((double)used/total*360):0;
        unsigned occupied_angle=total?(unsigned)((double)occupied/total*360):0;
        storage_arc(0,0,0x277AC7,true);
        if(used_angle) storage_arc(0,used_angle,0x277AC7,false);
        if(occupied_angle>used_angle) storage_arc(used_angle,occupied_angle,0xD29932,false);
        lv_obj_t *percentage=label(body,"");rect(percentage,16,70,60,24);
        lv_obj_set_style_text_align(percentage,LV_TEXT_ALIGN_CENTER,0);
        lv_label_set_text_fmt(percentage,"%u%%",total?(unsigned)((double)occupied/total*100):0);
        char bytes[24],text[64];
        storage_size(bytes,sizeof(bytes),used);snprintf(text,sizeof(text),"%s %s",storage_sd?"사용":"파일",bytes);
        lv_obj_t *item=label(body,text);rect(item,98,38,138,24);lv_obj_set_style_text_color(item,lv_color_hex(0x277AC7),0);
        storage_size(bytes,sizeof(bytes),free_bytes);snprintf(text,sizeof(text),"여유 %s",bytes);
        item=label(body,text);rect(item,98,66,138,24);lv_obj_set_style_text_color(item,lv_color_hex(0x398B63),0);
        storage_size(bytes,sizeof(bytes),reserved);
        if(storage_sd) snprintf(text,sizeof(text),"파일 시스템");
        else snprintf(text,sizeof(text),"시스템 %s",bytes);
        item=label(body,text);rect(item,98,94,138,24);lv_obj_set_style_text_color(item,lv_color_hex(0xD29932),0);
        storage_size(bytes,sizeof(bytes),total);snprintf(text,sizeof(text),"전체 %s",bytes);
        rect(label(body,text),4,124,232,24);
        lv_obj_t *location=label(body,"");rect(location,4,146,232,20);
        lv_label_set_text_fmt(location,"%u개 · %u/%u %s",storage_cache.count,storage_cache.count?storage_offset/3+1:0,(storage_cache.count+2)/3,storage_folder[0]?storage_folder:"/");
        for(unsigned i=0;i<storage_cache.shown;++i) {
            storage_view_entry_t *entry=&storage_cache.entries[i];
            lv_obj_t *row=button(body,"",storage_open_cb,(void *)(uintptr_t)i);rect(row,4,168+(int)i*21,166,20);
            lv_obj_set_style_pad_all(row,0,0);
            lv_obj_t *name=lv_obj_get_child(row,0);lv_label_set_text(name,entry->name);
            lv_obj_set_width(name,160);lv_label_set_long_mode(name,LV_LABEL_LONG_DOT);lv_obj_center(name);
            storage_size(bytes,sizeof(bytes),entry->bytes);
            rect(label(body,entry->directory?"폴더":bytes),174,168+(int)i*21,62,20);
        }
        if(!storage_cache.count) rect(label(body,"파일 없음"),4,176,232,24);
        lv_label_set_text(status,"상단 버튼: 새로고침");
    }
    lv_obj_t *prev=button(body,"이전",storage_step_cb,(void *)(intptr_t)-1);rect(prev,4,232,52,32);
    lv_obj_t *next=button(body,"다음",storage_step_cb,(void *)(intptr_t)1);rect(next,64,232,52,32);
    lv_obj_t *parent=button(body,"상위",storage_parent_cb,NULL);rect(parent,124,232,52,32);
    rect(button(body,"홈",home_cb,NULL),184,232,52,32);
    if(loading||storage_cache.error!=ESP_OK||storage_offset<3) lv_obj_add_state(prev,LV_STATE_DISABLED);
    if(loading||storage_cache.error!=ESP_OK||storage_offset+3>=storage_cache.count) lv_obj_add_state(next,LV_STATE_DISABLED);
    if(loading||!storage_folder[0]) lv_obj_add_state(parent,LV_STATE_DISABLED);
}
static void storage_request(void) {
    command_t command={.op=5,.sd=storage_sd,.offset=storage_offset};
    snprintf(command.folder,sizeof(command.folder),"%s",storage_folder);
    if(xQueueSend(commands,&command,0)!=pdTRUE) {lv_label_set_text(status,"처리 중 · 다시 시도하세요");return;}
    busy=true;storage_render(true);
}
static void update_stats(void) {
    net_mode_t mode=network_get_mode();
    m1_dashboard_update(mode==NET_MODE_STA_CONNECTED,mode==NET_MODE_AP_CAPTIVE,
        network_get_rssi(),web_server_is_running(),heap_caps_get_total_size(MALLOC_CAP_DEFAULT),
        heap_caps_get_free_size(MALLOC_CAP_DEFAULT),capacity_ready?&home_flash:NULL,capacity_ready?&home_sd:NULL);
}
static void home(void) {
    page=0;clear_page("Mini WEB M1");
    m1_dashboard_create(body,setup_cb,toggle_cb,storage_choose_cb);update_stats();
    rect(m1_dashboard_button(body,"설정",M1_ICON_SETTINGS,setup_cb),4,154,112,32);
    rect(m1_dashboard_button(body,"스토리지",M1_ICON_FILES,storage_home_cb),124,154,112,32);
    rect(m1_dashboard_button(body,"접속 QR",M1_ICON_QR,qr_cb),4,192,112,32);
    rect(m1_dashboard_button(body,"서버",M1_ICON_SERVER,toggle_cb),124,192,112,32);
    rect(m1_dashboard_button(body,"SoftAP",M1_ICON_AP,softap_cb),4,232,112,32);
    rect(m1_dashboard_button(body,"도움",M1_ICON_HELP,help_cb),124,232,112,32);
    if(pending_save_message) {lv_label_set_text(status,pending_save_message);pending_save_message=NULL;}
}
static void ui_task(void *arg) {
    home();
    command_t c={.op=2};
    if(network_has_saved_credentials(c.ssid,sizeof(c.ssid),c.pass,sizeof(c.pass))) {
        xQueueSend(commands,&c,0);busy=true;lv_label_set_text(status,"저장된 Wi-Fi 연결 중");
    }
    memset(c.pass,0,sizeof(c.pass));
    int64_t last=0;
    int64_t next_capacity_refresh=0;
    int64_t next_server_retry=0;
    while(1) {
        result_t r;
        if(xQueueReceive(results,&r,0)) {
            if(r.op!=6) busy=false;
            if(r.op==6) {
                capacity_pending=false;capacity_ready=true;
                home_flash=r.flash;home_sd=r.sd;
                if(page==0) update_stats();
            } else if(r.op==1 && page==1) {
                scan_count=r.count;scan_offset=0;
                memcpy(scan_cache,r.aps,scan_count*sizeof(scan_cache[0]));render_scan();
                if(r.err==ESP_OK) lv_label_set_text(status,r.count?"목록에서 선택":"검색 결과 없음");
                else lv_label_set_text_fmt(status,"검색 실패: %s",esp_err_to_name(r.err));
            } else if(r.op==5 && page==7) {
                storage_cache=r.storage;
                if(storage_offset>=storage_cache.count && storage_offset) {
                    storage_offset=storage_cache.count?(storage_cache.count-1)/3*3:0;
                    storage_request();
                } else storage_render(false);
            } else if(r.op==2 && network_get_mode()==NET_MODE_STA_CONNECTED) {
                pending_save_message=r.err==ESP_OK?"연결 완료":"저장 또는 서버 시작 실패";home();
            } else if(r.op==4) {
                softap();lv_label_set_text(status,r.err==ESP_OK?"SoftAP 준비 완료":"SoftAP 시작 실패");
            } else lv_label_set_text(status,r.err==ESP_OK?"완료":"실패: 다시 시도");
        }
        if(!busy && auto_server_requested && !server_running && network_get_mode()==NET_MODE_STA_CONNECTED &&
           esp_timer_get_time()>=next_server_retry) {
            command_t restart={.op=3};
            if(xQueueSend(commands,&restart,0)==pdPASS) {busy=true;next_server_retry=esp_timer_get_time()+5000000;}
        }
        if(page==0 && esp_timer_get_time()-last>1000000) {update_stats();last=esp_timer_get_time();}
        if(page==0 && !busy && !capacity_pending && esp_timer_get_time()>=next_capacity_refresh) {
            command_t refresh={.op=6};
            if(xQueueSend(commands,&refresh,0)==pdPASS) {
                capacity_pending=true;next_capacity_refresh=esp_timer_get_time()+5000000;
            }
        }
        lv_timer_handler();vTaskDelay(pdMS_TO_TICKS(10));
    }
}
void onboard_ui_start(void) {
    commands=xQueueCreate(1,sizeof(command_t));results=xQueueCreate(1,sizeof(result_t));
    if(!commands || !results) abort();
    if(xTaskCreate(worker,"m1_network",8192,NULL,3,NULL)!=pdPASS) abort();
    if(xTaskCreate(ui_task,"m1_ui",8192,NULL,4,NULL)!=pdPASS) abort();
}

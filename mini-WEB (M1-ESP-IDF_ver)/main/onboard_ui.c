#include "board_display.h"
#include "screen_saver.h"
#include "m1_sound.h"
#include "sdkconfig.h"
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
#include "esp_log.h"
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
static lv_obj_t *router_info;
static lv_obj_t *volume_button;
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
                if(network_router_saved()) {
                    esp_err_t router=network_router_start(false);
                    if(router!=ESP_OK) ESP_LOGW("M1_UI","Router mode restore failed: %s",esp_err_to_name(router));
                }
            } else {
                /* Failed saved/manual Wi-Fi must leave a usable setup portal. */
                r.op=4;
                r.err=network_start_softap();
                if(r.err==ESP_OK) r.err=dns_server_start();
                if(r.err==ESP_OK) r.err=web_server_start();
                server_running=r.err==ESP_OK;
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
        } else if(cmd.op==7) {
            r.err=cmd.offset?network_router_start(true):network_router_stop(true);
        } else if(cmd.op==8) {
            r.err=m1_sound_set_volume(cmd.offset);
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
static void router(void);
static void router_cb(lv_event_t *e) {if(!busy) router();}
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
    router_info=NULL;
    volume_button=NULL;
    lv_obj_clean(lv_scr_act());
    /* The canvas is deleted before its backing pixels are released. */
    free(qr_pixels); qr_pixels=NULL;
    fixed_container(lv_scr_act());
    lv_obj_set_style_text_font(lv_scr_act(),&font_m1,0);
    lv_obj_set_style_text_line_space(lv_scr_act(),0,0);
    lv_obj_t *heading=lv_label_create(lv_scr_act());
    lv_label_set_text(heading,title);lv_obj_align(heading,LV_ALIGN_TOP_MID,0,5);
    if(page==0) {
        rect(heading,4,5,128,20);
        lv_label_set_long_mode(heading,LV_LABEL_LONG_DOT);
    }
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
static void show_access_qr(int type);
/* Version 4 QR (up to 62 bytes) with a 4-module quiet zone at 4 px per module:
 * 164 px plus an optional white margin. The canvas is deleted before its
 * pixels are released (clear_page). */
static bool qr_canvas(const char *text,int x,int y,int margin) {
    QRCode code;uint8_t bits[512];
    if(qrcode_getBufferSize(4)>sizeof(bits) || qrcode_initText(&code,bits,4,ECC_MEDIUM,text)<0) return false;
    int size=(code.size+8)*4+margin*2;
    qr_pixels=malloc(LV_CANVAS_BUF_SIZE_INDEXED_1BIT(size,size));
    if(!qr_pixels) return false;
    lv_obj_t *canvas=lv_canvas_create(body);
    lv_canvas_set_buffer(canvas,qr_pixels,size,size,LV_IMG_CF_INDEXED_1BIT);
    lv_obj_set_pos(canvas,x,y);
    lv_canvas_set_palette(canvas,0,lv_color_white());
    lv_canvas_set_palette(canvas,1,lv_color_black());
    lv_color_t white={.full=0},black={.full=1};
    lv_canvas_fill_bg(canvas,white,LV_OPA_COVER);
    for(int my=0;my<code.size;++my) for(int mx=0;mx<code.size;++mx) if(qrcode_getModule(&code,mx,my)) {
        for(int dy=0;dy<4;++dy) for(int dx=0;dx<4;++dx)
            lv_canvas_set_px_color(canvas,margin+(mx+4)*4+dx,margin+(my+4)*4+dy,black);
    }
    return true;
}
/* Access QR: the visitor chooses how to reach the board. */
enum {QR_DOMAIN,QR_MDNS,QR_LOCAL,QR_IPV6,QR_ACCESS_COUNT};
static int access_qr=-1;
static const char *const access_names[QR_ACCESS_COUNT]={"도메인","mDNS","로컬","IPv6"};
static const char *const access_hints[QR_ACCESS_COUNT]={
    "인터넷 어디서나 접속","같은 Wi-Fi · 이름 주소 고정","같은 Wi-Fi · IP 주소","IPv6 지원 네트워크에서 접속"};
static bool access_url(int type,char *url,size_t length) {
    char addr[64]={0};int n=-1;
    if(!server_running || network_get_mode()!=NET_MODE_STA_CONNECTED) return false;
    if(type==QR_DOMAIN) return cloud_relay_get_url(url,length)==ESP_OK;
    if(type==QR_MDNS) return network_mdns_is_ready() && network_get_mdns_url(url,length)==ESP_OK;
    if(type==QR_LOCAL && network_get_ip_str(addr,sizeof(addr))==ESP_OK && addr[0] && strcmp(addr,"0.0.0.0"))
        n=snprintf(url,length,"http://%s/",addr);
    /* network_get_ip6_str reports "미발급" without a global address. */
    if(type==QR_IPV6 && network_get_ip6_str(addr,sizeof(addr))==ESP_OK && strchr(addr,':'))
        n=snprintf(url,length,"http://[%s]/",addr);
    return n>0 && (size_t)n<length;
}
static void access_qr_cb(lv_event_t *e) {if(!busy) show_access_qr((int)(intptr_t)lv_event_get_user_data(e));}
static void ap_wifi_qr_cb(lv_event_t *e) {if(!busy) show_qr(1);}
static void router_qr_cb(lv_event_t *e) {if(!busy) show_qr(3);}
static void ap_url_qr_cb(lv_event_t *e) {if(!busy) show_qr(2);}
static void show_access_qr(int type) {
    bool available[QR_ACCESS_COUNT];char url[96]={0};
    for(int i=0;i<QR_ACCESS_COUNT;++i) available[i]=access_url(i,url,sizeof(url));
    /* Default: the last choice if still usable, else domain, mDNS, local, IPv6. */
    if(type<0 || type>=QR_ACCESS_COUNT) {
        type=access_qr>=0 && available[access_qr]?access_qr:-1;
        for(int i=0;type<0 && i<QR_ACCESS_COUNT;++i) if(available[i]) type=i;
        if(type<0) type=QR_LOCAL;
    }
    access_qr=type;page=2;
    char title[32];snprintf(title,sizeof(title),"%s 접속 QR",access_names[type]);clear_page(title);
    if(!available[type] || !access_url(type,url,sizeof(url))) {
        const char *why=!server_running || network_get_mode()!=NET_MODE_STA_CONNECTED?"Wi-Fi 연결과 서버 실행이 필요합니다.":
            type==QR_DOMAIN?"외부 연결을 기다리는 중입니다.\n등록된 보드만 사용할 수 있습니다.":
            type==QR_MDNS?"mDNS 주소를 준비하는 중입니다.":type==QR_IPV6?"공유기에서 IPv6 주소를 받지 못했습니다.":"IP 주소가 아직 없습니다.";
        lv_obj_t *note=label(body,why);rect(note,4,40,232,80);lv_label_set_long_mode(note,LV_LABEL_LONG_WRAP);
        lv_label_set_text(status,"다른 접속 방식을 선택하세요");
    } else if(!qr_canvas(url,38,0,0)) {
        rect(label(body,"QR 생성 실패"),4,40,232,20);
    } else {
        /* The full address is shown (two lines at most), never truncated. */
        lv_obj_t *address=label(body,url);rect(address,4,166,232,40);lv_label_set_long_mode(address,LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_align(address,LV_TEXT_ALIGN_CENTER,0);
        lv_label_set_text(status,access_hints[type]);
    }
    for(int i=0;i<QR_ACCESS_COUNT;++i) {
        lv_obj_t *choice=button(body,access_names[i],access_qr_cb,(void *)(intptr_t)i);rect(choice,4+i*58,208,54,26);
        lv_obj_set_style_pad_all(choice,0,0);
        if(i==type) {lv_obj_set_style_bg_color(choice,lv_color_hex(0x243244),0);lv_obj_set_style_text_color(choice,lv_color_white(),0);}
        else if(!available[i]) lv_obj_add_state(choice,LV_STATE_DISABLED);
    }
    rect(button(body,"홈",home_cb,NULL),4,238,232,26);
}
static void show_qr(int kind) {
    if(kind==0) {show_access_qr(-1);return;}
    page=kind==3?10:kind+3;
    clear_page(kind==3?"공유기 연결 QR":kind==1?"SoftAP 연결 QR":"SoftAP 설정 QR");
    char url[96],pass[ROUTER_PASS_LEN+1]={0};
    net_mode_t mode=network_get_mode();
    if(kind==3?!network_router_active() || network_router_password(pass,sizeof(pass))!=ESP_OK :
       !server_running || mode!=NET_MODE_AP_CAPTIVE) {
        rect(label(body,"Wi-Fi / 서버 실행 필요"),4,24,232,20);
        rect(button(body,"홈",home_cb,NULL),4,232,232,32);return;
    }
    if(kind==3) snprintf(url,sizeof(url),"WIFI:T:WPA;S:%s;P:%s;;",network_router_ssid(),pass);
    else if(kind==1) snprintf(url,sizeof(url),"WIFI:T:nopass;S:%s;;",SOFTAP_SSID_DEFAULT);
    else snprintf(url,sizeof(url),"http://192.168.4.1/setup");
    if(!qr_canvas(url,30,0,8)) {
        rect(label(body,"QR 생성 실패"),4,24,232,20);
        rect(button(body,"홈",home_cb,NULL),4,232,232,32);return;
    }
    if(kind==3) {
        char text[48];
        lv_obj_t *qr_ssid=label(body,network_router_ssid());rect(qr_ssid,4,184,232,20);
        snprintf(text,sizeof(text),"비밀번호 %s",pass);rect(label(body,text),4,206,232,20);
        memset(pass,0,sizeof(pass));
        rect(button(body,"뒤로",router_cb,NULL),4,232,112,32);
        rect(button(body,"홈",home_cb,NULL),124,232,112,32);
        return;
    }
    lv_obj_t *qr_url=label(body,kind==1?SOFTAP_SSID_DEFAULT:url);
    rect(qr_url,4,184,232,20);lv_label_set_long_mode(qr_url,LV_LABEL_LONG_DOT);
    rect(label(body,kind==1?"스캔 후 Wi-Fi 가입 허용":"SoftAP 연결 후 열기"),4,206,232,20);
    rect(button(body,kind==1?"설정 QR":"연결 QR",kind==1?ap_url_qr_cb:ap_wifi_qr_cb,NULL),4,232,112,32);
    rect(button(body,"뒤로",softap_cb,NULL),124,232,112,32);
}
static void qr_cb(lv_event_t *e) {if(!busy) show_qr(network_get_mode()==NET_MODE_AP_CAPTIVE?2:0);}
static void router_toggle_cb(lv_event_t *e) {
    if(busy) return;
    bool on=!network_router_active();
    if(on && network_get_mode()!=NET_MODE_STA_CONNECTED) {lv_label_set_text(status,"먼저 공유기 Wi-Fi에 연결하세요");return;}
    command_t c={.op=7,.offset=on};
    if(xQueueSend(commands,&c,0)==pdTRUE) {busy=true;lv_label_set_text(status,on?"공유기 모드 켜는 중":"공유기 모드 끄는 중");}
}
static void router_render_info(void) {
    if(!router_info) return;
    char pass[ROUTER_PASS_LEN+1]={0},text[200];
    bool on=network_router_active(),uplink=network_get_mode()==NET_MODE_STA_CONNECTED;
    if(network_router_password(pass,sizeof(pass))!=ESP_OK) snprintf(pass,sizeof(pass),"-");
    if(on) snprintf(text,sizeof(text),"상태: 켜짐 · 접속 %d/%d대\nSSID: %s\n비밀번호: %s\n보드 주소: %s\n인터넷: %s",
                    network_router_clients(),ROUTER_MAX_CONN,network_router_ssid(),pass,network_router_ip(),uplink?"연결됨":"상위 Wi-Fi 끊김");
    else snprintf(text,sizeof(text),"상태: 꺼짐\n보드가 연결된 Wi-Fi의 인터넷을\n다른 기기와 공유합니다.\n상위 Wi-Fi: %s\n최대 %d대 · 화면 끄기 권장",
                  uplink?"연결됨":"연결 필요",ROUTER_MAX_CONN);
    memset(pass,0,sizeof(pass));
    lv_label_set_text(router_info,text);
}
static void router(void) {
    page=9;clear_page("공유기 모드");
    router_info=label(body,"");rect(router_info,4,0,232,148);lv_label_set_long_mode(router_info,LV_LABEL_LONG_WRAP);
    router_render_info();
    bool on=network_router_active();
    lv_obj_t *toggle=button(body,on?"공유기 모드 끄기":"공유기 모드 켜기",router_toggle_cb,NULL);rect(toggle,4,154,232,32);
    if(!on && network_get_mode()!=NET_MODE_STA_CONNECTED) lv_obj_add_state(toggle,LV_STATE_DISABLED);
    lv_obj_t *join=button(body,"가입 QR (비밀번호 포함)",router_qr_cb,NULL);rect(join,4,192,232,32);
    if(!on) lv_obj_add_state(join,LV_STATE_DISABLED);
    rect(button(body,"뒤로",softap_cb,NULL),4,232,112,32);
    rect(button(body,"홈",home_cb,NULL),124,232,112,32);
}
static void softap(void) {
    page=3;clear_page("SoftAP 설정");
    bool active=network_get_mode()==NET_MODE_AP_CAPTIVE && server_running;
    char text[256];
    snprintf(text,sizeof(text),"설정용 SoftAP: %s\nSSID: %s (비밀번호 없음)\n주소: 192.168.4.1/setup\n연결 QR로 가입 후 설정 QR",
             active?"실행":"정지",SOFTAP_SSID_DEFAULT);
    rect(label(body,text),4,0,232,112);
    rect(button(body,"공유기 모드 (인터넷 공유)",router_cb,NULL),4,116,232,32);
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
        /* The arc and legend already give the totals; the rest of the body is the file list. */
        for(unsigned i=0;i<storage_cache.shown;++i) {
            storage_view_entry_t *entry=&storage_cache.entries[i];
            int y=128+(int)i*34;
            lv_obj_t *row=button(body,"",storage_open_cb,(void *)(uintptr_t)i);rect(row,4,y,166,30);
            lv_obj_set_style_pad_all(row,0,0);
            lv_obj_t *name=lv_obj_get_child(row,0);lv_label_set_text(name,entry->name);
            lv_obj_set_width(name,160);lv_label_set_long_mode(name,LV_LABEL_LONG_DOT);lv_obj_center(name);
            storage_size(bytes,sizeof(bytes),entry->bytes);
            rect(label(body,entry->directory?"폴더":bytes),174,y+5,62,20);
        }
        if(!storage_cache.count) rect(label(body,"파일 없음"),4,160,232,24);
        if(storage_folder[0]) lv_label_set_text(status,storage_folder);
        else lv_label_set_text(status,"상단 버튼: 새로고침");
    }
    lv_obj_t *prev=button(body,"이전",storage_step_cb,(void *)(intptr_t)-1);rect(prev,4,232,52,32);
    lv_obj_t *next=button(body,"다음",storage_step_cb,(void *)(intptr_t)1);rect(next,64,232,52,32);
    lv_obj_t *parent=button(body,"상위",storage_parent_cb,NULL);rect(parent,124,232,52,32);
    rect(button(body,"홈",home_cb,NULL),184,232,52,32);
    if(loading||storage_cache.error!=ESP_OK||storage_offset<STORAGE_VIEW_ROWS) lv_obj_add_state(prev,LV_STATE_DISABLED);
    if(loading||storage_cache.error!=ESP_OK||storage_offset+STORAGE_VIEW_ROWS>=storage_cache.count) lv_obj_add_state(next,LV_STATE_DISABLED);
    if(loading||!storage_folder[0]) lv_obj_add_state(parent,LV_STATE_DISABLED);
}
static void storage_request(void) {
    command_t command={.op=5,.sd=storage_sd,.offset=storage_offset};
    snprintf(command.folder,sizeof(command.folder),"%s",storage_folder);
    if(xQueueSend(commands,&command,0)!=pdTRUE) {lv_label_set_text(status,"처리 중 · 다시 시도하세요");return;}
    busy=true;storage_render(true);
}
static void teardown_ui(void) {
    m1_keyboard_reset();
    m1_dashboard_reset();
    router_info=NULL;
    lv_obj_clean(lv_scr_act());
    free(qr_pixels); qr_pixels=NULL;
    body=status=NULL;
}
static void draw_screen_off(lv_event_t *e) {
    lv_area_t a;lv_obj_get_coords(lv_event_get_target(e),&a);
    int x=a.x1+(lv_area_get_width(&a)-18)/2,y=a.y1+(lv_area_get_height(&a)-16)/2;
    const struct {int x,y,w,h;uint32_t color;} parts[]={
        {0,0,18,12,0xFFFFFF},{2,2,14,8,0x243244},{7,12,4,2,0xFFFFFF},{4,14,10,2,0xFFFFFF}};
    lv_draw_rect_dsc_t dsc;lv_draw_rect_dsc_init(&dsc);dsc.bg_opa=LV_OPA_COVER;
    for(unsigned i=0;i<sizeof(parts)/sizeof(parts[0]);++i) {
        lv_area_t r={x+parts[i].x,y+parts[i].y,x+parts[i].x+parts[i].w-1,y+parts[i].y+parts[i].h-1};
        dsc.bg_color=lv_color_hex(parts[i].color);lv_draw_rect(lv_event_get_draw_ctx(e),&dsc,&r);
    }
}
static void sleep_cb(lv_event_t *e) {
    ESP_LOGI("M1_UI","Screen off requested: busy=%d page=%d",busy,page);
    if(busy) {lv_label_set_text(status,"작업 중 · 잠시 후 다시 시도");return;}
    lv_label_set_text(status,"화면 끄는 중");
    screen_saver_request();
}
static void volume_render(void) {
    if(!volume_button) return;
    lv_obj_t *text=lv_obj_get_child(volume_button,0);
    unsigned level=m1_sound_get_volume();
    if(level) lv_label_set_text_fmt(text,"음량 %u",level);
    else lv_label_set_text(text,"무음");
    lv_obj_center(text);
}
static void volume_cb(lv_event_t *e) {
    (void)e;
    if(busy) return;
    command_t cmd={.op=8,.offset=(m1_sound_get_volume()+1)%5};
    if(xQueueSend(commands,&cmd,0)==pdTRUE) {
        busy=true;
        lv_label_set_text(status,"음량 저장 중");
    }
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
    volume_button=button(lv_scr_act(),"",volume_cb,NULL);rect(volume_button,138,2,60,24);
    lv_obj_set_style_pad_all(volume_button,0,0);
    volume_render();
#ifndef CONFIG_M1_SYSTEM_SOUNDS
    lv_obj_add_state(volume_button,LV_STATE_DISABLED);
#endif
    /* A dark monitor, not a power symbol: only the display turns off. */
    lv_obj_t *sleep=lv_btn_create(lv_scr_act());rect(sleep,202,2,34,24);
    lv_obj_add_event_cb(sleep,sleep_cb,LV_EVENT_CLICKED,NULL);
    lv_obj_add_event_cb(sleep,draw_screen_off,LV_EVENT_DRAW_MAIN_END,NULL);
    if(pending_save_message) {lv_label_set_text(status,pending_save_message);pending_save_message=NULL;}
}
static void ui_task(void *arg) {
    home();
    command_t c={.op=2};
    bool saved=network_has_saved_credentials(c.ssid,sizeof(c.ssid),c.pass,sizeof(c.pass));
    if(!saved) c.op=4;
    xQueueSend(commands,&c,0);busy=true;
    lv_label_set_text(status,saved?"저장된 Wi-Fi 연결 중":"SoftAP 설정 준비 중");
    memset(c.pass,0,sizeof(c.pass));
    int64_t last=0;
    int64_t next_capacity_refresh=0;
    int64_t next_server_retry=0;
    while(1) {
        result_t r;
        /* While asleep no widgets exist: keep state, drop rendering, and let
         * home() show it on wake. The worker blocks until results is drained. */
        if(screen_saver_active() && xQueueReceive(results,&r,0)) {
            if(r.op!=6) busy=false;
            if(r.op==6) {capacity_pending=false;capacity_ready=true;home_flash=r.flash;home_sd=r.sd;}
            else if(r.op==2 && network_get_mode()==NET_MODE_STA_CONNECTED)
                pending_save_message=r.err==ESP_OK?"연결 완료":"저장 또는 서버 시작 실패";
        } else if(!screen_saver_active() && xQueueReceive(results,&r,0)) {
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
            } else if(r.op==7) {
                if(page==9) router();
                lv_label_set_text(status,r.err==ESP_OK?(network_router_active()?"공유기 모드 켜짐":"공유기 모드 꺼짐"):
                                  r.err==ESP_ERR_INVALID_STATE?"상위 Wi-Fi 연결 필요":"공유기 모드 전환 실패");
            } else if(r.op==4) {
                softap();lv_label_set_text(status,r.err==ESP_OK?"SoftAP 준비 완료":"SoftAP 시작 실패");
            } else if(r.op==8) {
                if(page==0) volume_render();
                lv_label_set_text(status,r.err==ESP_OK?"음량 저장 완료":"음량 저장 실패");
            } else lv_label_set_text(status,r.err==ESP_OK?"완료":"실패: 다시 시도");
        }
        if(!busy && auto_server_requested && !server_running && network_get_mode()==NET_MODE_STA_CONNECTED &&
           esp_timer_get_time()>=next_server_retry) {
            command_t restart={.op=3};
            if(xQueueSend(commands,&restart,0)==pdPASS) {busy=true;next_server_retry=esp_timer_get_time()+5000000;}
        }
        bool asleep=screen_saver_active();
        if(!asleep && (page==0 || page==9) && esp_timer_get_time()-last>1000000) {
            if(page==0) update_stats(); else router_render_info();
            last=esp_timer_get_time();
        }
        if(!asleep && page==0 && !busy && !capacity_pending && esp_timer_get_time()>=next_capacity_refresh) {
            command_t refresh={.op=6};
            if(xQueueSend(commands,&refresh,0)==pdPASS) {
                capacity_pending=true;next_capacity_refresh=esp_timer_get_time()+5000000;
            }
        }
        if(screen_saver_service(page==0 && !busy, teardown_ui)) {
            home();
            screen_saver_reveal();
        }
        /* The draw buffer is freed while asleep; LVGL must not render then. */
        if(!screen_saver_active()) lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(asleep?50:10));
    }
}
void onboard_ui_start(void) {
    commands=xQueueCreate(1,sizeof(command_t));results=xQueueCreate(1,sizeof(result_t));
    if(!commands || !results) abort();
    if(xTaskCreate(worker,"m1_network",6144,NULL,3,NULL)!=pdPASS) abort();
    if(xTaskCreate(ui_task,"m1_ui",7168,NULL,4,NULL)!=pdPASS) abort();
}

#include "board_display.h"
#include "network_manager.h"
#include "web_server.h"
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

typedef struct {int op;char ssid[33];char pass[65];} command_t;
typedef struct {int op;esp_err_t err;int count;wifi_scan_item_t aps[12];} result_t;
static QueueHandle_t commands,results;
static lv_obj_t *body,*status,*ssid,*password,*keyboard,*input,*scan_list;
static lv_obj_t *pager,*page_label;
static lv_obj_t *key_matrix,*key_controls[3],*setup_actions[3];
static bool keyboard_open;
static int key_mode,key_page;
static char key_chars[12][2];
static const char *key_map[20];
static wifi_scan_item_t scan_cache[12];
static int scan_count,scan_offset;
static bool busy;
static atomic_bool server_running;
static const char *key_sets[]={"qwertyuiopasdfghjklzxcvbnm1234567890",
    "QWERTYUIOPASDFGHJKLZXCVBNM1234567890",
    "!@#$%^&*()_-+=[]{}\\|;:'\",./?`~<>"};
static int page;
static int help_index;
static const char *pending_save_message;
static void home(void);
static void setup(void);
static void softap(void);
static void help(void);
static void keyboard_visible(bool visible);
static void keyboard_map(void);
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
            if(server_running) {web_server_stop();server_running=false;}
            else {r.err=web_server_start();server_running=r.err==ESP_OK;}
        } else if(cmd.op==4) {
            dns_server_stop();
            if(server_running) {web_server_stop();server_running=false;}
            r.err=network_start_softap();
            if(r.err==ESP_OK) r.err=dns_server_start();
            if(r.err==ESP_OK) {
                r.err=web_server_start();server_running=r.err==ESP_OK;
            }
            if(r.err!=ESP_OK) dns_server_stop();
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
        keyboard_visible(false);
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
static void home_cb(lv_event_t *e) {if(!busy && !keyboard_open) home();}
static void softap_cb(lv_event_t *e) {if(!busy) softap();}
static void ap_start_cb(lv_event_t *e) {submit(4);}
static void help_cb(lv_event_t *e) {if(!busy) {help_index=0;help();}}
static void keyboard_visible(bool visible) {
    keyboard_open=visible;
    for(int i=0;i<3;++i) {
        if(visible) lv_obj_add_state(setup_actions[i],LV_STATE_DISABLED);
        else lv_obj_clear_state(setup_actions[i],LV_STATE_DISABLED);
    }
    if(visible) {
        lv_obj_clear_flag(keyboard,LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(scan_list,LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(pager,LV_OBJ_FLAG_HIDDEN);
        keyboard_map();
    } else {
        lv_obj_add_flag(keyboard,LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(scan_list,LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(pager,LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(status,"목록으로 복귀: 입력 유지");
    }
}
static void pick_cb(lv_event_t *e) {
    int index=(int)(intptr_t)lv_event_get_user_data(e);
    if(index<0 || index>=scan_count || busy) return;
    lv_textarea_set_text(ssid,scan_cache[index].ssid);
    lv_textarea_set_text(password,"");
    input=password;
    keyboard_visible(true);
}
static void focus_cb(lv_event_t *e) {
    input=lv_event_get_target(e);
    keyboard_visible(true);
}
static void keyboard_map(void) {
    const char *chars=key_sets[key_mode];int remaining=(int)strlen(chars)-key_page*12;
    int count=remaining>12?12:remaining,j=0;
    for(int i=0;i<count;++i) {
        if(i==6) key_map[j++]="\n";
        key_chars[i][0]=chars[key_page*12+i];key_chars[i][1]=0;
        key_map[j++]=key_chars[i];
    }
    key_map[j++]="\n";key_map[j++]="SPACE";key_map[j++]="DEL";key_map[j]="";
    lv_btnmatrix_set_map(key_matrix,key_map);
    lv_btnmatrix_set_btn_ctrl_all(key_matrix,LV_BTNMATRIX_CTRL_CLICK_TRIG|LV_BTNMATRIX_CTRL_NO_REPEAT);
    lv_label_set_text(lv_obj_get_child(key_controls[0],0),key_mode==1?"abc":"ABC");
    lv_label_set_text(lv_obj_get_child(key_controls[1],0),key_mode==2?"영문":"특문");
    lv_label_set_text_fmt(status,"%s / %s %d/3",input==ssid?"SSID":"비밀번호",
                          key_mode==2?"특문":key_mode==1?"대문자":"소문자",key_page+1);
}
static void key_control_cb(lv_event_t *e) {
    int action=(int)(intptr_t)lv_event_get_user_data(e);
    lv_indev_t *device=lv_indev_get_act();if(device) lv_indev_wait_release(device);
    if(action==3) {keyboard_visible(false);return;}
    if(action==0) {key_mode=key_mode==1?0:1;key_page=0;}
    else if(action==1) {key_mode=key_mode==2?0:2;key_page=0;}
    else key_page=(key_page+1)%3;
    keyboard_map();
}
static void key_cb(lv_event_t *e) {
    uint16_t id=lv_btnmatrix_get_selected_btn(key_matrix);
    if(id==LV_BTNMATRIX_BTN_NONE || !input || !keyboard_open) return;
    const char *key=lv_btnmatrix_get_btn_text(key_matrix,id);
    if(!strcmp(key,"DEL")) lv_textarea_del_char(input);
    else if(!strcmp(key,"SPACE")) lv_textarea_add_text(input," ");
    else lv_textarea_add_text(input,key);
}
static void clear_page(const char *title) {
    lv_obj_clean(lv_scr_act());
    input=NULL;
    keyboard_open=false;
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
    keyboard=lv_obj_create(body);rect(keyboard,2,112,236,152);fixed_container(keyboard);
    const char *controls[]={"ABC","특문","다음","목록"};
    const int positions[]={0,58,116,174};
    for(int i=0;i<4;++i) {
        lv_obj_t *control=button(keyboard,controls[i],key_control_cb,(void *)(intptr_t)i);
        rect(control,positions[i],0,i==3?62:54,32);
        if(i<3) key_controls[i]=control;
    }
    key_matrix=lv_btnmatrix_create(keyboard);rect(key_matrix,0,36,236,116);
    lv_obj_set_style_pad_all(key_matrix,2,0);
    lv_obj_set_style_pad_row(key_matrix,4,0);lv_obj_set_style_pad_column(key_matrix,4,0);
    lv_obj_set_style_text_font(key_matrix,&lv_font_montserrat_14,LV_PART_ITEMS);
    lv_obj_set_style_pad_all(key_matrix,0,LV_PART_ITEMS);
    key_mode=0;key_page=0;keyboard_map();
    lv_obj_add_flag(keyboard,LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(key_matrix,key_cb,LV_EVENT_VALUE_CHANGED,NULL);
    lv_obj_add_event_cb(ssid,focus_cb,LV_EVENT_CLICKED,NULL);
    lv_obj_add_event_cb(password,focus_cb,LV_EVENT_CLICKED,NULL);
    lv_label_set_text(status,"");
}
static void show_qr(int kind);
static void ap_wifi_qr_cb(lv_event_t *e) {if(!busy) show_qr(1);}
static void ap_url_qr_cb(lv_event_t *e) {if(!busy) show_qr(2);}
static void show_qr(int kind) {
    page=kind?kind+3:2;clear_page(kind==1?"SoftAP 연결 QR":kind==2?"SoftAP 설정 QR":"웹 접속 QR");
    char ip[32]={0},url[96];
    net_mode_t mode=network_get_mode();
    if(!server_running || (kind?mode!=NET_MODE_AP_CAPTIVE:mode!=NET_MODE_STA_CONNECTED)) {
        rect(label(body,"Wi-Fi / 서버 실행 필요"),4,24,232,20);
        rect(button(body,"홈",home_cb,NULL),4,232,232,32);return;
    }
    if(kind==1) snprintf(url,sizeof(url),"WIFI:T:nopass;S:%s;;",SOFTAP_SSID_DEFAULT);
    else if(kind==2) snprintf(url,sizeof(url),"http://192.168.4.1/setup");
    else {network_get_ip_str(ip,sizeof(ip));snprintf(url,sizeof(url),"http://%s/",ip);}
    QRCode code;uint8_t bits[512];
    if(qrcode_getBufferSize(4)>sizeof(bits) || qrcode_initText(&code,bits,4,ECC_MEDIUM,url)<0) {
        rect(label(body,"QR 생성 실패"),4,24,232,20);
        rect(button(body,"홈",home_cb,NULL),4,232,232,32);return;
    }
    /* 4-module white quiet zone, integer scaling, no interpolation. */
    static uint8_t qr_pixels[LV_CANVAS_BUF_SIZE_INDEXED_1BIT(180,180)];
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
    rect(label(body,kind==1?SOFTAP_SSID_DEFAULT:url),4,184,232,20);
    rect(label(body,kind==1?"스캔 후 Wi-Fi 가입 허용":kind==2?"SoftAP 연결 후 열기":"같은 Wi-Fi에서 접속"),4,206,232,20);
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
    "4. 연결 완료 후 접속\n휴대폰을 같은 공유기의\nWi-Fi로 다시 연결하세요.\n보드 홈에서 접속 QR을\n열고 카메라로 읽으세요.\n저장한 mDNS 주소도 사용.\n이후 부팅은 자동 연결.\n보드 설정도 계속 사용 가능.\nSoftAP 종료: 보드 설정에서\n공유기 연결 또는 재부팅."
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
static lv_obj_t *metrics;
static void update_stats(void) {
    size_t total=0,used=0;esp_err_t disk=esp_spiffs_info("littlefs",&total,&used);
    char ip[32]="-";network_get_ip_str(ip,sizeof(ip));
    char text[240];
    snprintf(text,sizeof(text),"IP: %s\nWi-Fi: %s / %d dBm\n서버: %s\n홈 요청: %lu\n가동: %lld s\n여유 RAM: %lu KB\n저장: %u / %u KB%s",
        network_get_mode()==NET_MODE_STA_CONNECTED || network_get_mode()==NET_MODE_AP_CAPTIVE?ip:"-",
        network_get_mode()==NET_MODE_AP_CAPTIVE?"SoftAP":network_get_mode()==NET_MODE_STA_CONNECTED?"연결":"끊김",network_get_rssi(),
        server_running?"실행":"정지",(unsigned long)web_server_home_requests(),
        (long long)(esp_timer_get_time()/1000000),(unsigned long)(esp_get_free_heap_size()/1024),
        (unsigned)(used/1024),(unsigned)(total/1024),disk==ESP_OK?"":" (오류)");
    lv_label_set_text(metrics,text);
}
static void home(void) {
    page=0;clear_page("Mini WEB M1");metrics=label(body,"");rect(metrics,4,0,232,148);update_stats();
    rect(button(body,"보드 설정",setup_cb,NULL),4,154,112,32);
    rect(button(body,"SoftAP",softap_cb,NULL),124,154,112,32);
    rect(button(body,"접속 QR",qr_cb,NULL),4,192,112,32);
    rect(button(body,"서버 제어",toggle_cb,NULL),124,192,112,32);
    rect(button(body,"사용법",help_cb,NULL),4,232,232,32);
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
    while(1) {
        result_t r;
        if(xQueueReceive(results,&r,0)) {
            busy=false;
            if(r.op==1 && page==1) {
                scan_count=r.count;scan_offset=0;
                memcpy(scan_cache,r.aps,scan_count*sizeof(scan_cache[0]));render_scan();
                if(r.err==ESP_OK) lv_label_set_text(status,r.count?"목록에서 선택":"검색 결과 없음");
                else lv_label_set_text_fmt(status,"검색 실패: %s",esp_err_to_name(r.err));
            } else if(r.op==2 && network_get_mode()==NET_MODE_STA_CONNECTED) {
                pending_save_message=r.err==ESP_OK?"연결 완료":"저장 또는 서버 시작 실패";home();
            } else if(r.op==4) {
                softap();lv_label_set_text(status,r.err==ESP_OK?"SoftAP 준비 완료":"SoftAP 시작 실패");
            } else lv_label_set_text(status,r.err==ESP_OK?"완료":"실패: 다시 시도");
        }
        if(page==0 && esp_timer_get_time()-last>1000000) {update_stats();last=esp_timer_get_time();}
        lv_timer_handler();vTaskDelay(pdMS_TO_TICKS(10));
    }
}
void onboard_ui_start(void) {
    commands=xQueueCreate(1,sizeof(command_t));results=xQueueCreate(1,sizeof(result_t));
    if(!commands || !results) abort();
    if(xTaskCreate(worker,"m1_network",8192,NULL,3,NULL)!=pdPASS) abort();
    if(xTaskCreate(ui_task,"m1_ui",8192,NULL,4,NULL)!=pdPASS) abort();
}

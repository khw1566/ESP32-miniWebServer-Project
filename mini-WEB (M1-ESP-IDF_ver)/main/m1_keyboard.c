#include "m1_keyboard.h"
#include <stdint.h>
#include <string.h>

enum { LOWER, UPPER, SYMBOLS, MODE_COUNT };
/* Maps remain unchanged for the entire lifetime of the button matrix. */
static const char *maps[MODE_COUNT][44] = {
    {
        "1","2","3","4","5","6","7","8","9","0","\n",
        "q","w","e","r","t","y","u","i","o","p","\n",
        "a","s","d","f","g","h","j","k","l","\n",
        "z","x","c","v","b","n","m","SP","DEL",""
    },
    {
        "1","2","3","4","5","6","7","8","9","0","\n",
        "Q","W","E","R","T","Y","U","I","O","P","\n",
        "A","S","D","F","G","H","J","K","L","\n",
        "Z","X","C","V","B","N","M","SP","DEL",""
    },
    {
        "!","@","#","$","%","^","&","*","(",")","\n",
        "_","-","+","=","[","]","{","}","\\","|","\n",
        ";",":","'","\"",",",".","/","?","`","~","\n",
        "<",">","SP","DEL",""
    }
};
static struct {
    lv_obj_t *root, *matrix, *target, *status, *list, *pager, *actions[3], *controls[2];
    const char *field;
    unsigned mode;
    bool open;
} keyboard;

static void place(lv_obj_t *obj,int x,int y,int width,int height) {
    lv_obj_set_pos(obj,x,y);lv_obj_set_size(obj,width,height);
}
static void set_map(void) {
    lv_btnmatrix_set_selected_btn(keyboard.matrix,LV_BTNMATRIX_BTN_NONE);
    lv_btnmatrix_set_map(keyboard.matrix,maps[keyboard.mode]);
    lv_btnmatrix_set_btn_ctrl_all(keyboard.matrix,LV_BTNMATRIX_CTRL_CLICK_TRIG|LV_BTNMATRIX_CTRL_NO_REPEAT);
    unsigned keys=keyboard.mode==SYMBOLS?34:38;
    lv_btnmatrix_set_btn_width(keyboard.matrix,(uint16_t)(keys-2),2);
    lv_btnmatrix_set_btn_width(keyboard.matrix,(uint16_t)(keys-1),2);
    lv_label_set_text(lv_obj_get_child(keyboard.controls[0],0),keyboard.mode==UPPER?"abc":"ABC");
    lv_label_set_text(lv_obj_get_child(keyboard.controls[1],0),keyboard.mode==SYMBOLS?"영문":"특문");
    lv_label_set_text_fmt(keyboard.status,"%s / %s",keyboard.field?keyboard.field:"",
        keyboard.mode==SYMBOLS?"특문":keyboard.mode==UPPER?"대문자":"소문자");
}
void m1_keyboard_reset(void) {memset(&keyboard,0,sizeof(keyboard));}
bool m1_keyboard_is_open(void) {return keyboard.open;}
void m1_keyboard_close(void) {
    if(!keyboard.root||!keyboard.open) return;
    keyboard.open=false;keyboard.target=NULL;keyboard.field=NULL;
    lv_obj_add_flag(keyboard.root,LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(keyboard.list,LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(keyboard.pager,LV_OBJ_FLAG_HIDDEN);
    for(unsigned i=0;i<3;++i) lv_obj_clear_state(keyboard.actions[i],LV_STATE_DISABLED);
    lv_label_set_text(keyboard.status,"목록으로 복귀: 입력 유지");
}
void m1_keyboard_open(lv_obj_t *textarea,const char *field_name) {
    if(!keyboard.root||!textarea) return;
    keyboard.target=textarea;keyboard.field=field_name;keyboard.open=true;
    lv_obj_add_flag(keyboard.list,LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(keyboard.pager,LV_OBJ_FLAG_HIDDEN);
    for(unsigned i=0;i<3;++i) lv_obj_add_state(keyboard.actions[i],LV_STATE_DISABLED);
    lv_obj_clear_flag(keyboard.root,LV_OBJ_FLAG_HIDDEN);
    set_map();
}
static void control_cb(lv_event_t *event) {
    if(!keyboard.open) return;
    unsigned action=(unsigned)(uintptr_t)lv_event_get_user_data(event);
    /* CLICKED is already a released touch: waiting again drops the next tap. */
    if(action==2) {m1_keyboard_close();return;}
    if(action==0) keyboard.mode=keyboard.mode==UPPER?LOWER:UPPER;
    else if(action==1) keyboard.mode=keyboard.mode==SYMBOLS?LOWER:SYMBOLS;
    set_map();
}
static void key_cb(lv_event_t *event) {
    if(!keyboard.open||!keyboard.target) return;
    const uint32_t *selected=lv_event_get_param(event);
    unsigned keys=keyboard.mode==SYMBOLS?34:38;
    if(!selected||*selected==LV_BTNMATRIX_BTN_NONE||*selected>=keys) return;
    const char *key=lv_btnmatrix_get_btn_text(keyboard.matrix,(uint16_t)*selected);
    if(!key||!key[0]) return;
    if(!strcmp(key,"DEL")) lv_textarea_del_char(keyboard.target);
    else lv_textarea_add_text(keyboard.target,!strcmp(key,"SP")?" ":key);
}
void m1_keyboard_create(lv_obj_t *parent,lv_obj_t *status,lv_obj_t *list,
                        lv_obj_t *pager,lv_obj_t *const actions[3]) {
    m1_keyboard_reset();
    keyboard.status=status;keyboard.list=list;keyboard.pager=pager;
    for(unsigned i=0;i<3;++i) keyboard.actions[i]=actions[i];
    keyboard.root=lv_obj_create(parent);place(keyboard.root,2,112,236,152);
    lv_obj_clear_flag(keyboard.root,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(keyboard.root,0,0);
    lv_obj_set_style_border_width(keyboard.root,0,0);
    lv_obj_set_style_radius(keyboard.root,0,0);
    const char *captions[]={"ABC","특문","목록"};
    for(unsigned i=0;i<3;++i) {
        lv_obj_t *button=lv_btn_create(keyboard.root);
        place(button,(int)i*80,120,76,32);
        lv_obj_set_style_pad_all(button,0,0);
        lv_obj_clear_flag(button,LV_OBJ_FLAG_CLICK_FOCUSABLE);
        lv_obj_t *label=lv_label_create(button);lv_label_set_text(label,captions[i]);lv_obj_center(label);
        lv_obj_add_event_cb(button,control_cb,LV_EVENT_CLICKED,(void *)(uintptr_t)i);
        if(i<2) keyboard.controls[i]=button;
    }
    keyboard.matrix=lv_btnmatrix_create(keyboard.root);place(keyboard.matrix,0,0,236,116);
    lv_obj_clear_flag(keyboard.matrix,LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_obj_set_style_pad_all(keyboard.matrix,2,0);
    lv_obj_set_style_pad_row(keyboard.matrix,3,0);lv_obj_set_style_pad_column(keyboard.matrix,1,0);
    lv_obj_set_style_text_font(keyboard.matrix,&lv_font_montserrat_14,LV_PART_ITEMS);
    lv_obj_set_style_pad_all(keyboard.matrix,0,LV_PART_ITEMS);
    set_map();
    lv_obj_add_event_cb(keyboard.matrix,key_cb,LV_EVENT_VALUE_CHANGED,NULL);
    lv_obj_add_flag(keyboard.root,LV_OBJ_FLAG_HIDDEN);
}

// Execute the C wake policy and UI/board control flow with host mocks. No compiler.
const fs=require('fs'),path=require('path'),vm=require('vm'),assert=require('node:assert/strict');
const root=path.resolve(__dirname,'..'),read=p=>fs.readFileSync(path.join(root,p),'utf8');
const policy=read('main/include/m3_screen_wake.h'),ui=read('main/m3_ui.c'),board=read('main/m3_board.c');
function body(name,src){const m=new RegExp('\\b'+name+'\\([^)]*\\)\\s*\\{').exec(src);assert(m,name);
 let i=m.index+m[0].length,start=i,depth=1;while(depth){if(src[i]==='{')depth++;if(src[i]==='}')depth--;i++;}return src.slice(start,i-1);}
function translate(s){return s.replace(/\/\*[\s\S]*?\*\//g,'').replaceAll('->','.')
 .replace(/\(uint32_t\)\((now - s\.(?:start|last_tap))\)/g,'(($1) >>> 0)');}
const c={M3_WAKE_HOLD:0,M3_WAKE_DOUBLE_TAP:1};vm.createContext(c);
vm.runInContext('function sample(s,mode,now,pressed,x,y){'+translate(body('m3_screen_wake_sample',policy))+'}',c);
function reset(){return {blocked:true,down:false,moved:false,tap:false,fired:false,start:0,last_tap:0,x:0,y:0,tap_x:0,tap_y:0};}
function sample(s,mode,now,down,x=240,y=400){return c.sample(s,mode,now>>>0,down,x,y);}
let s=reset();
assert(!sample(s,0,0,true));assert(!sample(s,0,3000,true),'off press must drain');
sample(s,0,3010,false);sample(s,0,3100,true);
assert(!sample(s,0,5099,true));assert(sample(s,0,5100,true));assert(!sample(s,0,5200,true),'one wake per sleep');
s=reset();sample(s,0,0,false);sample(s,0,100,true);sample(s,0,500,true,300,400);
assert(!sample(s,0,2200,true,240,400),'drag cancels hold even if finger moves back');
sample(s,0,2300,false);sample(s,0,2400,true);assert(sample(s,0,4400,true));
s=reset();sample(s,0,0,false);sample(s,0,10,true);sample(s,0,1800,false);
sample(s,0,2000,true);assert(!sample(s,0,2500,true),'holds cannot accumulate across release');
function taps(gap=120,x=240,firstDuration=80){s=reset();sample(s,1,0,false);sample(s,1,100,true);
 sample(s,1,100+firstDuration,false);sample(s,1,100+firstDuration+gap,true,x);
 return sample(s,1,180+firstDuration+gap,false,x);}
assert(taps());assert(!taps(500),'slow taps');assert(!taps(120,350),'distant taps');
assert(!taps(120,240,300),'long press is not a tap');
s=reset();sample(s,1,0,false);sample(s,1,100,true);sample(s,1,150,true,290);
sample(s,1,200,false,240);sample(s,1,250,true);assert(!sample(s,1,300,false),'drag is not first tap');
s=reset();sample(s,1,0,false);sample(s,1,100,true);assert(!sample(s,1,3000,true),'hold cannot wake double tap mode');
s=reset();sample(s,0,0xfffffe00,false);sample(s,0,0xffffff00,true);
assert(sample(s,0,0xffffff00+2000,true),'hold handles uint32 tick wrap');
s=reset();sample(s,1,0xffffff00,false);sample(s,1,0xffffff80,true);sample(s,1,0xffffffc0,false);
sample(s,1,0x10,true);assert(sample(s,1,0x50,false),'double tap handles uint32 tick wrap');

// Run screen-off admission and wake/release barrier from the actual UI source.
const u={screen_blanked:false,screen_waking:false,screen_release_at:0,screen_input:null,screen_wake:reset(),
 screen_shield:{hidden:true},shade_note:{},settings_info:{},ui_preferences:{wake_mode:0},
 LV_INDEV_TYPE_POINTER:1,LV_INDEV_STATE_PRESSED:1,LV_OBJ_FLAG_HIDDEN:1,
 ESP_OK:0,NULL:null,T_SET_WAKE_UNAVAILABLE:11,T_SET_BRIGHTNESS_ERROR:12,
 input:{},screen_timer:{},now:0,pressed:false,point:{x:240,y:400},touch:true,offError:0,wakeError:0,power:[],
 lv_indev_get_next:input=>input?null:u.input,lv_indev_get_type:()=>1,lv_indev_get_display:()=>1,
 m3_board_display:()=>1,m3_board_touch_ready:()=>u.touch,m3_screen_wake_reset:out=>Object.assign(out,reset()),
 lv_obj_move_to_index:()=>{},lv_obj_remove_flag:obj=>obj.hidden=false,lv_obj_add_flag:obj=>obj.hidden=true,
 m3_board_set_screen_on:on=>{u.power.push(on);return on?u.wakeError:u.offError;},set_text:(obj,t)=>obj.text=t,tr:x=>x,theme_wallpaper_sync:()=>{},
 lv_tick_get:()=>u.now,lv_indev_get_state:()=>u.pressed?1:0,lv_indev_get_point:(_in,p)=>Object.assign(p,u.point),
 m3_screen_wake_sample:(state,mode,now,down,x,y)=>c.sample(state,mode,now>>>0,down,x,y)};
vm.createContext(u);
const tick=body('screen_wake_tick',ui).replace('lv_point_t point;','let point={};').replaceAll('&point','point')
 .replaceAll('&screen_wake','screen_wake').replace(/\b(?:bool|uint32_t|esp_err_t) (\w+)/g,'let $1')
 .replace('(uint32_t)(now - screen_release_at)','((now - screen_release_at) >>> 0)');
const off=body('screen_off',ui).replace('for (lv_indev_t *input','for (let input').replaceAll('&screen_wake','screen_wake');
vm.runInContext('function off(event){'+off.replace('(void)event;','')+'}\nfunction tick(timer){'+tick.replace('(void)timer;','')+'}',u);
u.touch=false;u.off(null);assert.equal(u.power.length,0);assert(u.screen_shield.hidden);
u.touch=true;u.screen_timer=null;u.off(null);assert.equal(u.power.length,0,'cannot blank without wake timer');
u.screen_timer={};u.offError=5;u.off(null);assert(!u.screen_blanked);assert(u.screen_shield.hidden);
u.offError=0;u.off(null);assert(u.screen_blanked);assert(!u.screen_shield.hidden);
u.tick(null);u.now=100;u.pressed=true;u.tick(null);u.now=2100;u.tick(null);
assert(!u.screen_blanked&&u.screen_waking);assert(!u.screen_shield.hidden,'hold release cannot hit apps');
u.now=2500;u.tick(null);assert(!u.screen_shield.hidden,'finger still down');
u.pressed=false;u.now=2600;u.tick(null);assert(!u.screen_shield.hidden);
u.now=2700;u.tick(null);assert(u.screen_shield.hidden&&!u.screen_waking);
u.off(null);u.tick(null);u.now=2800;u.pressed=true;u.tick(null);u.now=4800;u.wakeError=5;u.tick(null);
u.pressed=false;u.now=5000;u.tick(null);assert(u.screen_shield.hidden,'backlight error must not trap touch shield');
assert(/if \(!screen_blanked && applied_brightness/.test(ui),'preference polling must not relight a sleeping screen');
assert(ui.includes('lv_obj_create(lv_layer_top())'),'shield must cover all screen siblings');
assert(ui.includes('LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_EVENT_BUBBLE'));

// Board duty zero and saved-brightness restore, including PWM failure fallback.
const b={backlight_pwm_ready:true,ESP_OK:0,LEDC_LOW_SPEED_MODE:0,LEDC_CHANNEL_0:0,M3_LCD_BACKLIGHT:23,
 duty:99,gpio:1,fail:0,brightness:37,
 ledc_set_duty:(_m,_c,d)=>{b.duty=d;return b.fail;},ledc_update_duty:()=>0,
 ledc_stop:(_m,_c,idle)=>b.gpio=idle,gpio_set_level:(_pin,v)=>{b.gpio=v;return 0;},
 m3_preferences_get:out=>out.brightness=b.brightness,m3_board_set_brightness:v=>{b.restored=v;return 0;}};
vm.createContext(b);vm.runInContext('function power(on){'+body('m3_board_set_screen_on',board)
 .replace('m3_preferences_t p;','let p={};').replace('&p','p').replace('esp_err_t err','let err')+'}',b);
assert.equal(b.power(false),0);assert.equal(b.duty,0);b.power(true);assert.equal(b.restored,37);
b.fail=5;assert.equal(b.power(false),5);assert.equal(b.gpio,1);assert(!b.backlight_pwm_ready);
b.power(false);assert.equal(b.gpio,0);b.power(true);assert.equal(b.gpio,1);

// Reboot needs explicit confirmation and reuses settings/SD/audio busy guards.
const r={restart_confirm:{hidden:true},restart_info:{},LV_OBJ_FLAG_HIDDEN:1,LV_ANIM_OFF:0,
 calls:0,result:0,M3_MODE_OK:0,M3_MODE_BUSY:1,M3_MODE_RESTART_FAILED:3,
 T_SET_RESTARTING:10,T_RESTART_BUSY:11,T_SET_RESTART_MANUAL:12,T_SET_SAVE_FAILED:13,
 lv_event_get_user_data:e=>e.action,lv_obj_remove_flag:o=>o.hidden=false,lv_obj_add_flag:o=>o.hidden=true,
 lv_obj_scroll_to_view:()=>{},m3_preferences_get:o=>Object.assign(o,{wake_mode:1,brightness:37}),
 m3_preferences_restart_with:p=>{r.calls++;r.saved={...p};return r.result;},set_text:(o,t)=>o.text=t,tr:x=>x};
vm.createContext(r);vm.runInContext('function restart(event){'+body('system_restart',ui)
 .replace('unsigned action = (unsigned)(uintptr_t)','let action = ')
 .replace('m3_preferences_t p;','let p={};').replaceAll('&p','p').replace('m3_mode_result_t result','let result')+'}',r);
r.restart({action:0});assert(!r.restart_confirm.hidden);assert.equal(r.calls,0);
r.restart({action:1});assert(r.restart_confirm.hidden);assert.equal(r.calls,0);
r.restart({action:0});r.restart({action:2});assert.equal(r.calls,1);assert.equal(r.saved.wake_mode,1);
for(const [result,message] of [[0,10],[1,11],[2,13],[3,12]]){r.result=result;r.restart({action:2});assert.equal(r.restart_info.text,message);}
console.log('PASS: real C hold/double-tap policy, drag/slow-tap rejection, tick wrap, input drain, touch shield release barrier, brightness restore/fallback and confirmed guarded reboot. Host mocks only.');

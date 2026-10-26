// Execute the small C settings policies against NVS/task/SD mocks. No target compilation.
const fs=require('fs'),path=require('path'),vm=require('vm'),assert=require('node:assert/strict');
const root=path.resolve(__dirname,'..'),read=p=>fs.readFileSync(path.join(root,p),'utf8');
const src=read('main/m3_preferences.c');
function body(name,text=src){const m=new RegExp('\\b'+name+'\\([^)]*\\)\\s*\\{').exec(text);assert(m,name);
 let i=m.index+m[0].length,start=i,depth=1;while(depth){if(text[i]==='{')depth++;if(text[i]==='}')depth--;i++;}return text.slice(start,i-1);}
function js(s){return s.replace(/\/\*[\s\S]*?\*\//g,'').replace(/port(?:ENTER|EXIT)_CRITICAL\(&lock\);/g,'')
 .replace(/\b(?:unsigned|int|bool|esp_err_t) (\w+)/g,'let $1').replace(/\b(\d+)U\b/g,'$1')
 .replace(/\(uint8_t\)/g,'').replace(/sizeof\([^)]*\)/g,'8').replaceAll('->','.')
 .replace(/atomic_store\(&restart_phase, (\d+)\)/g,'restart_phase = $1');}
const defaults=()=>({version:1,brightness:100,max_fps:30,animations:1,web_focus:0,volume:40,last_volume:40,wake_mode:0});
function setup(){
 const c={current:defaults(),sound_effect:0,revision:0,dirty:false,web_active:false,next_save:0,now:0,restart_phase:0,
 storage_lock:{},ESP_OK:0,ESP_ERR_INVALID_ARG:1,ESP_ERR_TIMEOUT:2,NVS_READWRITE:1,pdTRUE:1,pdPASS:1,NULL:null,
 M3_MODE_OK:0,M3_MODE_BUSY:1,M3_MODE_SAVE_FAILED:2,M3_MODE_RESTART_FAILED:3,
 saved:null,writes:0,tasks:0,fail:false,taskFail:false,lockFail:false,sdBusy:false,playing:false,recording:false,agent:false,download:false,
 esp_timer_get_time:()=>c.now,pdMS_TO_TICKS:x=>x,ESP_LOGW:()=>{},esp_err_to_name:x=>String(x),
 memcmp:(a,b)=>JSON.stringify(a)===JSON.stringify(b)?0:1,
 xSemaphoreTake:()=>c.lockFail?0:1,xSemaphoreGive:()=>{},
 nvs_open:()=>0,nvs_set_blob:(_n,_key,p)=>{c.writes++;c.pending={...p};return c.fail?5:0;},
 nvs_set_u8:(_n,key,value)=>{c.pendingSound=value;return c.fail?5:0;},
 nvs_commit:()=>{if(c.fail)return 5;c.saved={...c.pending};return 0;},nvs_close:()=>{},
 m3_audio_state:out=>out.playing=c.playing,m3_recorder_state:out=>out.busy=c.recording,
 m3_agent_state:out=>out.busy=c.agent,m3_tube_download_state:out=>out.busy=c.download,sd_media_ready:()=>true,
 sd_media_admin_lock:()=>!c.sdBusy,sd_media_admin_unlock:()=>{},restart_device:()=>{},
 xTaskCreate:()=>{c.tasks++;return c.taskFail?0:1;}};
 const decl=s=>s.replace(/m3_preferences_t (\w+);/g,'let $1={};')
  .replace('nvs_handle_t nvs;','let nvs={};').replaceAll('&nvs','nvs').replaceAll('&p','p')
  .replaceAll('&current','current').replace('current = *p','current = {...p}')
  .replace('*out = current','Object.assign(out,current)');
 let mode=body('m3_preferences_restart_with')
  .replace('if (!atomic_compare_exchange_strong(&restart_phase, &expected, 1)) return expected == 2 ? M3_MODE_OK : M3_MODE_BUSY;',
   'if (restart_phase !== 0) return restart_phase === 2 ? M3_MODE_OK : M3_MODE_BUSY; restart_phase=1;')
  .replace('m3_audio_state_t audio;','let audio={};').replace('m3_recorder_state_t recorder;','let recorder={};')
  .replace('m3_agent_state_t agent;','let agent={};').replace('m3_tube_download_t download;','let download={};')
  .replaceAll('&download','download').replaceAll('&audio','audio').replaceAll('&recorder','recorder').replaceAll('&agent','agent');
 vm.createContext(c);
 for(const [name,args,code] of [
 ['m3_preferences_valid','p',body('m3_preferences_valid')],['m3_preferences_frame_ms','fps',body('m3_preferences_frame_ms')],
 ['m3_preferences_get','out',body('m3_preferences_get')],['m3_preferences_update','p',body('m3_preferences_update')],
 ['m3_preferences_volume','percent',body('m3_preferences_volume')],['write_preferences','p',body('write_preferences')],
 ['m3_preferences_save','p',body('m3_preferences_save')],['m3_preferences_poll','',body('m3_preferences_poll')],
 ['m3_preferences_boot_mode','force_setup',body('m3_preferences_boot_mode')],['m3_preferences_restart_with','p',mode]]){
  let translated=js(decl(code));if(name==='m3_preferences_frame_ms')translated=translated.replace('return (1000 + fps - 1) / fps','return Math.floor((1000 + fps - 1) / fps)');
  vm.runInContext('function '+name+'('+args+'){'+translated+'}',c);
 }
 return c;
}
let c=setup();assert(c.m3_preferences_valid(defaults()));
assert(c.m3_preferences_valid({...defaults(),wake_mode:1}),'double tap choice is valid; old zero reserved byte remains hold');
c.m3_preferences_update({...defaults(),wake_mode:1});c.m3_preferences_poll();
assert.equal(c.saved.wake_mode,1,'wake choice persists without changing the eight-byte NVS layout');
c=setup();
c=setup();
for(const patch of [{brightness:4},{brightness:101},{max_fps:4},{max_fps:61},{volume:151},{last_volume:0},{animations:2},{web_focus:2},{version:2},{wake_mode:2}])assert(!c.m3_preferences_valid({...defaults(),...patch}));
for(let fps=5;fps<=60;fps++){const ms=c.m3_preferences_frame_ms(fps);assert(ms>0);assert(1000/ms<=fps);}
assert.equal(c.m3_preferences_frame_ms(0),200);assert.equal(c.m3_preferences_frame_ms(100),17);
c.m3_preferences_volume(80);c.m3_preferences_volume(0);assert.equal(c.current.volume,0);assert.equal(c.current.last_volume,80);
c.m3_preferences_volume(c.current.last_volume);assert.equal(c.current.volume,80);c.m3_preferences_volume(200);assert.equal(c.current.volume,150);
c.m3_preferences_poll();assert.equal(c.saved.volume,150);assert(!c.dirty);
c.m3_preferences_update({...c.current,brightness:25});c.fail=true;c.now=2000000;c.m3_preferences_poll();assert(c.dirty);assert.equal(c.saved.brightness,100);
c.fail=false;c.now=4000000;c.m3_preferences_poll();assert.equal(c.saved.brightness,25);assert(!c.dirty);
c.current.web_focus=1;c.m3_preferences_boot_mode(false);assert(c.web_active);c.m3_preferences_boot_mode(true);assert(!c.web_active);assert.equal(c.current.web_focus,1,'BOOT override preserves stored choice');
for(const flag of ['playing','recording','agent','download','sdBusy']){c=setup();c[flag]=true;assert.equal(c.m3_preferences_restart_with({...defaults(),web_focus:1}),1);assert.equal(c.writes,0);assert.equal(c.tasks,0);}
c=setup();c.fail=true;assert.equal(c.m3_preferences_restart_with({...defaults(),web_focus:1}),2);assert.equal(c.tasks,0);assert.equal(c.current.web_focus,0);
c=setup();c.taskFail=true;assert.equal(c.m3_preferences_restart_with({...defaults(),web_focus:1}),3);assert.equal(c.saved.web_focus,1);
c=setup();assert.equal(c.m3_preferences_restart_with({...defaults(),web_focus:1}),0);assert.equal(c.tasks,1);assert.equal(c.saved.web_focus,1);
assert.equal(c.m3_preferences_restart_with({...defaults(),web_focus:1}),0);assert.equal(c.tasks,1,'duplicate apply cannot create multiple restart tasks');
const main=read('main/main.c'),runtime=read('main/m3_runtime.c'),web=read('main/web_server.c'),ui=read('main/m3_ui.c');
const focus=/if \(m3_preferences_web_active\(\)\) \{([\s\S]*?)return;\s*\}/.exec(main)[1];
assert(focus.includes('m3_runtime_start(false)'));assert(!/m3_(board_init|audio_start|agent_start|tube_start|ui_start)/.test(focus));
assert(main.indexOf('m3_preferences_web_active()')<main.indexOf('m3_board_init()'));
const normalPoll=/if \(!m3_preferences_web_active\(\)\) \{([^}]+)\}/.exec(runtime)[1];
for(const poll of ['m3_music_poll();','m3_photos_poll();','m3_sd_apps_poll();']) assert(normalPoll.includes(poll));
assert(web.includes('m3_preferences_web_active() && i >= 3'));
assert(web.includes('if (!m3_preferences_web_active()) httpd_register_uri_handler(s_server, &agent_uri)'));
assert(web.includes('"/device-settings"'));assert(web.includes('"/api/device-settings"'));
const motion={ui_preferences:{animations:1},LV_ANIM_ON:1,LV_ANIM_OFF:0};vm.createContext(motion);
vm.runInContext('function duration(ms){'+body('ui_duration',ui)+'} function animation(){'+body('ui_animation',ui)+'}',motion);
assert.equal(motion.duration(180),180);motion.ui_preferences.animations=0;assert.equal(motion.duration(180),0);assert.equal(motion.animation(),0);
assert(body('activity_set',ui).includes('if (!ui_preferences.animations)'));
assert(ui.includes('lv_display_get_refr_timer(display), m3_preferences_frame_ms(applied_fps)'));
assert(read('main/CMakeLists.txt').includes('"m3_preferences.c"'));assert(read('main/m3_board.c').includes('.freq_hz = 500'));
const pwm={backlight_pwm_ready:true,ESP_OK:0,ESP_ERR_INVALID_STATE:9,LEDC_LOW_SPEED_MODE:0,LEDC_CHANNEL_0:0,
 duty:0,fail:false,stopped:false,ledc_set_duty:(_mode,_channel,duty)=>{pwm.duty=duty;return pwm.fail?5:0;},
 ledc_update_duty:()=>0,ledc_stop:(_mode,_channel,idle)=>{assert.equal(idle,1);pwm.stopped=true;}};
vm.createContext(pwm);
vm.runInContext('function brightness(percent){'+body('m3_board_set_brightness',read('main/m3_board.c'))
 .replace('percent * 1023U / 100U','Math.floor(percent * 1023 / 100)').replace('esp_err_t err','let err')+'}',pwm);
pwm.brightness(0);assert.equal(pwm.duty,51);pwm.brightness(50);assert.equal(pwm.duty,511);
pwm.brightness(150);assert.equal(pwm.duty,1023);pwm.fail=true;assert.equal(pwm.brightness(25),5);
assert(pwm.stopped&&!pwm.backlight_pwm_ready);assert.equal(pwm.brightness(50),9);
console.log('PASS: C settings validation, FPS ceilings, mute/restore, debounced NVS retry, boot override, guarded mode save/restart, duplicate admission, headless startup/service/AI gating and motion policy. Host mocks only.');

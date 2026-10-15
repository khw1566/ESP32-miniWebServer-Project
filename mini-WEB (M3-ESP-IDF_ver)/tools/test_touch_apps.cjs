// Host checks execute the small UI policies from the C source. No firmware build.
const fs=require('fs'),path=require('path'),vm=require('vm'),assert=require('assert');
const root=path.resolve(__dirname,'..'),read=p=>fs.readFileSync(path.join(root,p),'utf8');
const ui=read('main/m3_ui.c'),runtime=read('main/m3_runtime.c'),config=read('sdkconfig.defaults');
function body(name,source=ui){
 const match=new RegExp('\\b'+name+'\\([^)]*\\)\\s*\\{').exec(source);assert(match,name);
 let start=match.index+match[0].length,end=start,depth=1;
 // These policy functions contain no braces inside strings or comments.
 while(depth){if(source[end]==='{')depth++;if(source[end]==='}')depth--;end++;}
 return source.slice(start,end-1);
}
const context={HOME_TOP:756,APP_HOME:0,swipe_started:false,swipe_origin:{x:0,y:0},homes:0,
 home_pill:{},feedback:0,settles:0,lv_anim_delete:()=>{},
 home_feedback:(_,value)=>context.feedback=value,home_settle:()=>{context.settles++;context.feedback=0;},
 LV_EVENT_PRESSED:1,LV_EVENT_PRESSING:2,LV_EVENT_RELEASED:3,LV_EVENT_PRESS_LOST:4,
 lv_indev_active:()=>true,lv_event_get_code:e=>e.code,
 lv_indev_get_point:(_,p)=>Object.assign(p,context.point),
 show_app:()=>context.homes++};
vm.createContext(context);
vm.runInContext('function home_swipe(dx,dy){'+body('home_swipe')+'}\nfunction gesture(event){'+body('home_gesture')
 .replace(/lv_indev_t \*input/g,'let input').replace(/lv_event_code_t code/g,'let code')
 .replace('int dx','let dx').replace('int progress','let progress')
 .replace('lv_point_t point;','let point = {};').replace('&point','point')+'}',context);
function touch(code,x,y){context.point={x,y};context.gesture({code});}
touch(1,240,780);assert.equal(context.feedback,10,'bar responds on press');
touch(2,240,745);assert.equal(context.feedback,50,'bar tracks drag distance');
touch(2,240,711);assert.equal(context.homes,0,'short drag');
touch(2,240,710);assert.equal(context.homes,1,'70px upward drag');
touch(2,240,600);assert.equal(context.homes,1,'one navigation per drag');
touch(1,240,700);touch(2,240,500);assert.equal(context.homes,1,'app scrolling must not exit');
touch(1,240,780);touch(2,331,680);assert.equal(context.homes,1,'horizontal drag');
assert.equal(context.feedback,0,'horizontal drag must reset feedback');
touch(1,240,780);touch(2,240,799);touch(3,240,799);touch(2,240,650);assert.equal(context.homes,1,'release cancels');
touch(1,240,780);touch(4,240,779);touch(2,240,650);assert.equal(context.homes,1,'lost press cancels');
assert.equal(context.feedback,0,'cancelled swipe settles bar');
touch(1,240,780);touch(2,150,710);assert.equal(context.homes,2,'diagonal tolerance');
context.lv_indev_active=()=>null;touch(1,240,780);assert.equal(context.homes,2);
const bar={home_progress:0,MAGENTA:0xf548ba,CYAN:0x00e5ef,LV_OPA_50:128,
 lv_obj_set_size:(obj,w,h)=>Object.assign(obj,{w,h}),
 lv_obj_set_pos:(obj,x,y)=>Object.assign(obj,{x,y}),
 lv_color_hex:value=>value,lv_color_mix:(a,b,mix)=>mix,
 lv_obj_set_style_bg_color:(obj,color)=>obj.color=color,
 lv_obj_set_style_shadow_color:()=>{},lv_obj_set_style_shadow_width:()=>{},lv_obj_set_style_shadow_opa:()=>{}};
vm.createContext(bar);vm.runInContext('function feedback(obj,progress){'+body('home_feedback')
 .replace('int32_t width','let width')+'}',bar);
const pill={};let lastWidth=0;
for(let progress=0;progress<=100;progress++){
 bar.feedback(pill,progress);assert(pill.w>=lastWidth);lastWidth=pill.w;
 assert.equal(pill.x+pill.w/2,240,'home bar stays centered');
 assert.equal(pill.y+pill.h,35,'bar expands upward inside the home strip');
 assert(pill.y>=0&&pill.y+pill.h<=44);assert(pill.color>=0&&pill.color<=255);
}
bar.feedback(pill,0);assert.equal(pill.w,120);assert.equal(pill.h,5);
bar.feedback(pill,100);assert.equal(pill.w,220);assert.equal(pill.h,10);
// Execute field opening with the LVGL keyboard's real default BOTTOM_MID alignment.
// set_pos alone would retain that alignment and put the keyboard below the screen.
const edit={HOME_TOP:756,CONTENT_TOP:100,KEYBOARD_HEIGHT:270,
 M3_LANG_JA:2,m3_i18n_language:()=>0,LV_KEYBOARD_MODE_USER_3:6,LV_KEYBOARD_MODE_USER_4:7,
 keyboard:{hidden:true,align:'bottom',y:0},home_strip:{},viewport:{},field:{},form_dirty:false,
 LV_EVENT_VALUE_CHANGED:10,LV_EVENT_FOCUSED:11,LV_EVENT_CLICKED:12,LV_EVENT_PRESSED:13,
 LV_OBJ_FLAG_HIDDEN:1,LV_ALIGN_TOP_LEFT:'top',LV_ANIM_ON:1,ui_duration:x=>x,ui_animation:()=>1,
 LV_KEYBOARD_MODE_USER_1:4,LV_KEYBOARD_MODE_NUMBER:3,LV_KEYBOARD_MODE_TEXT_LOWER:0,
 lv_event_get_code:e=>e.code,lv_event_get_current_target:()=>edit.field,lv_event_get_user_data:e=>e.mode,
 lv_obj_has_flag:obj=>obj.hidden,lv_obj_remove_flag:obj=>obj.hidden=false,
 lv_keyboard_set_mode:(_,mode)=>edit.mode=mode,lv_keyboard_set_textarea:(_,field)=>edit.target=field,
 lv_obj_align:(obj,align,x,y)=>Object.assign(obj,{align,x,y}),lv_obj_move_to_index:()=>{},
 lv_obj_set_height:(obj,h)=>obj.height=h,lv_obj_update_layout:()=>{},lv_obj_scroll_to_view_recursive:()=>{},
 lv_anim_delete:()=>{},lv_anim_init:()=>{},lv_anim_set_var:()=>{},lv_anim_set_exec_cb:()=>{},
 lv_anim_set_values:()=>{},lv_anim_set_duration:()=>{},lv_anim_set_path_cb:()=>{},lv_anim_start:()=>{},
 keyboard_motion:()=>{},lv_anim_path_ease_out:()=>{}};
vm.createContext(edit);
vm.runInContext('function edit_field(event){'+body('edit_field')
 .replace('lv_event_code_t code','let code').replace('lv_obj_t *field','let field')
 .replace('bool opening','let opening').replace('uintptr_t input_mode','let input_mode')
 .replace('(uintptr_t)','').replace('lv_anim_t animation;','let animation = {};')
 .replaceAll('&animation','animation')+'}',edit);
for(const code of [11,12,13])for(const mode of [0,1,2]){
 edit.keyboard.hidden=true;edit.keyboard.align='bottom';edit.edit_field({code,mode});
 assert.equal(edit.keyboard.hidden,false);assert.equal(edit.keyboard.align,'top');
 assert.equal(edit.keyboard.y,486);assert.equal(edit.keyboard.y+270,756);
 assert.equal(edit.viewport.height,386);assert.equal(edit.target,edit.field);
 assert.equal(edit.mode,mode===2?4:mode===1?3:0);
}
// Execute the actual keyboard dispatcher with textarea/mode mocks. Function keys are
// LVGL symbol macros in C; here each macro gets a readable stand-in value.
Object.assign(context,{keyboard:{},field:{text:''},key:'',hidden:0,mode:0,
 KEY_SHIFT:'SHIFT',KEY_DEL:'DEL',KEY_HIDE:'HIDE',KEY_OK:'OK',KEY_KO:'한',KEY_EN:'ABC',KEY_SYM:'?123',
 KEY_JA:'かな',KEY_KATA:'カナ',KEY_DAKU:'゛',KEY_SEMI:'゜',KEY_SMALL:'小',
 M3_KANA_VOICED:0,M3_KANA_SEMIVOICED:1,M3_KANA_SMALL:2,kana_edit:()=>{},
 LV_BUTTONMATRIX_BUTTON_NONE:65535,LV_KEYBOARD_MODE_TEXT_UPPER:1,LV_KEYBOARD_MODE_TEXT_LOWER:0,LV_KEYBOARD_MODE_SPECIAL:2,
 LV_KEYBOARD_MODE_USER_1:4,LV_KEYBOARD_MODE_USER_2:5,LV_KEYBOARD_MODE_USER_3:6,LV_KEYBOARD_MODE_USER_4:7,
 lv_buttonmatrix_get_selected_button:()=>0,lv_buttonmatrix_get_button_text:()=>context.key,
 lv_keyboard_get_textarea:()=>context.field,strcmp:(a,b)=>a===b?0:1,
 lv_keyboard_set_mode:(_,mode)=>context.mode=mode,hide_keyboard:()=>context.hidden++,
 lv_keyboard_get_mode:()=>context.mode,ready:0,LV_EVENT_READY:31,
 lv_obj_send_event:(obj,code)=>{if(code===31&&obj===context.field)context.ready++;},
 lv_textarea_delete_char:field=>field.text=field.text.slice(0,-1),
 lv_textarea_add_text:(field,text)=>field.text+=text});
vm.runInContext('function keyboard_key(event){'+body('keyboard_key')
 .replace('(void)event;','').replace('uint32_t index','let index').replace('lv_keyboard_mode_t mode','let mode')
 .replace('const char *key','const key').replace('lv_obj_t *field','let field').replace(/\bNULL\b/g,'null')
 .replace('(unsigned char)key[0]','key.charCodeAt(0)').replace('char text[385];','let text;')
 .replace('const char *current','const current')+'}',context);
function key(text){context.key=text;context.keyboard_key({});}
for(const char of 'My WiFi_9!')key(char);
assert.equal(context.field.text,'My WiFi_9!');key('DEL');assert.equal(context.field.text,'My WiFi_9');
key('SHIFT');assert.equal(context.mode,1);key('SHIFT');assert.equal(context.mode,0);key('?123');assert.equal(context.mode,2);
key('한');assert.equal(context.mode,4);key('SHIFT');assert.equal(context.mode,5);key('SHIFT');assert.equal(context.mode,4);
key('ABC');assert.equal(context.mode,0);assert.equal(context.field.text,'My WiFi_9','mode keys never type');
key('OK');key('HIDE');assert.equal(context.hidden,2);assert.equal(context.ready,1,'OK submits the field (READY), close does not');
context.field.text='';for(const c of '192.168.1.20')key(c);assert.equal(context.field.text,'192.168.1.20');
context.field.text='';key('かな');assert.equal(context.mode,6);key('あ');key('カナ');assert.equal(context.mode,7);key('カ');
assert.equal(context.field.text,'あカ');key('ABC');
// Each text layout can switch to the other layouts, delete, type a space and close.
const switches={keys_lower:['KEY_KO','KEY_SYM'],keys_upper:['KEY_KO','KEY_SYM'],keys_korean:['KEY_EN','KEY_SYM'],
 keys_korean_shift:['KEY_EN','KEY_SYM'],keys_special:['KEY_EN','KEY_KO']};
for(const [name,targets] of Object.entries(switches)){
 const start=ui.indexOf('static const char *const '+name+'[] = {');assert(start>=0,name);
 const map=ui.slice(start,ui.indexOf('};',start));
 for(const k of ['KEY_DEL','KEY_HIDE','KEY_OK','KEY_SPACE',...targets])assert(map.includes(k),name+' '+k);
}
// All printable ASCII characters remain available for SSIDs/passwords.
const maps=[...ui.matchAll(/static const char \*const keys_\w+\[\] = \{([\s\S]*?)\n\};/g)]
 .map(m=>[...m[1].matchAll(/"(?:\\.|[^"\\])*"/g)].map(s=>JSON.parse(s[0])));
assert.equal(maps.length,8);const chars=new Set(maps.flat().filter(s=>s.length===1));chars.add(' ');
for(let c=32;c<=126;c++)assert(chars.has(String.fromCharCode(c)),`missing ASCII ${c}`);
for(const name of ['KEYBOARD','TEXTAREA','BUTTONMATRIX','SWITCH'])assert(config.includes(`CONFIG_LV_USE_${name}=y`));
assert(!ui.includes('lv_tabview_'));assert(ui.includes('LV_OBJ_FLAG_PRESS_LOCK'));
assert(ui.includes('HOME_TOP - CONTENT_TOP - KEYBOARD_HEIGHT'));
assert(ui.includes('lv_obj_scroll_to_view_recursive(field, ui_animation())'));
assert(ui.includes('network_settings_valid(&settings)'));assert(ui.includes('strlen(ssid) > 32'));
assert(ui.includes('lv_textarea_set_password_mode(pass_field, true)'));
assert(runtime.includes('atomic_compare_exchange_strong(&wifi_pending'));
assert(runtime.includes('xQueueSend(wifi_queue, &request, 0)'));
assert(runtime.indexOf('network_save_configuration(&request->settings)')<runtime.indexOf('network_connect_sta(request->settings.ssid'));
assert(runtime.includes('memset(next->settings.pass, 0, sizeof(next->settings.pass))'));
assert(runtime.includes('memset(request, 0, sizeof(*request))'));
// Execute worker control flow with radio/NVS mocks; skip only C buffer formatting.
function removeCall(source,name,replacement){
 let result='',offset=0;
 for(;;){const match=new RegExp('\\b'+name+'\\s*\\(').exec(source.slice(offset));if(!match)return result+source.slice(offset);
  const start=offset+match.index;let end=start+match[0].length,depth=1,quote=false;
  while(depth){const ch=source[end++];if(ch==='\\'&&quote){end++;continue;}if(ch==='"'){quote=!quote;continue;}
   if(!quote){if(ch==='(')depth++;if(ch===')')depth--;}}
  result+=source.slice(offset,start)+replacement;offset=end;
 }
}
let worker=removeCall(body('wifi_execute',runtime),'snprintf','record_message()')
 .replace('memset(next->settings.pass, 0, sizeof(next->settings.pass))','next->settings.pass = ""')
 .replace('memset(request, 0, sizeof(*request))','request.cleared = true')
 .replace(/->/g,'.').replace(/\b(?:esp_err_t|int) (\w+)\s*=/g,'let $1 =')
 .replace('next.settings = request.settings','next.settings = {...request.settings}')
 .replace(/&request.settings/g,'request.settings')
 .replace('atomic_store(&wifi_pending, false)','pending = false');
worker=worker.replaceAll('60000000LL','60000000').replace('request.settings.pass[0] != 0','request.settings.pass.length > 0');
const radio={ESP_OK:0,ESP_ERR_INVALID_STATE:259,NET_MODE_IDLE:0,NET_MODE_AP_CAPTIVE:1,M3_WIFI_SCAN:0,M3_WIFI_CONNECT:1,M3_WIFI_SETUP:2,M3_WIFI_IPV6:3,M3_WIFI_CONNECT_SAVED:4,M3_WIFI_RESULTS:8,
 setup_requested:false,next_wifi_retry:0,now:0,esp_timer_get_time:()=>radio.now,
 restore_wifi_password:()=>false,
 pending:true,events:[],saved:0,connected:0,fallback:0,scanned:2,web:true,
 record_message:()=>{},publish:()=>radio.events.push('publish'),
 network_scan_wifi:()=>{radio.events.push('scan');return radio.scanned;},
 network_save_configuration:()=>{radio.events.push('save');return radio.saved;},
 dns_server_stop:()=>radio.events.push('dns-stop'),dns_server_start:()=>radio.events.push('dns-start'),
 network_connect_sta:()=>{radio.events.push('connect');return radio.connected;},
 network_start_softap:()=>{radio.events.push('setup');return radio.fallback;},
 network_retry_ipv6:()=>{radio.events.push('ipv6');return radio.connected;},
 web_server_is_running:()=>radio.web,network_get_mode:()=>1,
 web_server_start:()=>radio.events.push('http'),cloud_relay_start:()=>radio.events.push('relay')};
vm.createContext(radio);vm.runInContext('function execute(next,request){'+worker+'}',radio);
function execute(action,overrides={}){
 Object.assign(radio,{pending:true,events:[],saved:0,connected:0,fallback:0,scanned:2,web:true},overrides);
 const state={networks:[],scan_revision:0,settings_revision:0,wifi_revision:0};
 const request={action,settings:{ssid:'Example',pass:'secret123'}};radio.execute(state,request);
 assert(request.cleared);assert(!radio.pending);assert.equal(state.wifi_revision,1);return state;
}
let state=execute(1);assert.deepEqual(radio.events,['publish','save','dns-stop','connect','publish']);
assert.equal(state.settings.pass,'');assert.equal(state.settings_revision,1);
execute(1,{saved:5});assert(!radio.events.includes('connect'));assert(!radio.events.includes('setup'));
execute(1,{connected:5});assert.deepEqual(radio.events,['publish','save','dns-stop','connect','setup','dns-start','publish']);
execute(1,{connected:5,fallback:5});assert(!radio.events.includes('dns-start'));
execute(2,{web:false});assert.deepEqual(radio.events,['publish','setup','dns-start','http','relay','publish']);
execute(3);assert.deepEqual(radio.events,['publish','ipv6','publish']);
execute(3,{connected:5});assert.deepEqual(radio.events,['publish','ipv6','publish']);
execute(4);assert(!radio.events.includes('save'),'missing saved secret must not overwrite NVS');
state=execute(4,{restore_wifi_password:s=>{s.pass='saved-secret';return true;}});
assert(radio.events.includes('connect'));assert(state.wifi_password_saved);assert.equal(state.settings.pass,'');
assert(!radio.setup_requested);
execute(2);assert(radio.setup_requested,'explicit setup disables automatic handover');
for(const scanned of [-1,0,8]){state=execute(0,{scanned});assert.equal(state.scan_count,Math.max(0,scanned));assert.equal(state.scan_revision,1);assert(!radio.events.includes('save'));}
// Execute periodic recovery with the real worker; initial failure leaves a retryable portal.
let recovery=body('retry_saved_wifi',runtime).replaceAll('60000000LL','60000000')
 .replace('wifi_request_t request = {.action = M3_WIFI_CONNECT, .retry = true};','let request = {action:M3_WIFI_CONNECT,retry:true,settings:{}};')
 .replace('bool expected = false','let expected = false')
 .replace('atomic_compare_exchange_strong(&wifi_pending, &expected, true)','claim_wifi()')
 .replaceAll('&request.settings','request.settings').replaceAll('&request','request')
 .replace('memset(request, 0, sizeof(request))','request.cleared = true').replaceAll('->','.');
Object.assign(radio,{loaded:true,mode:1,clients:false,network_get_mode:()=>radio.mode,network_setup_has_clients:()=>radio.clients,
 network_settings_load:s=>{Object.assign(s,{ssid:'Saved',pass:'saved-secret'});return radio.loaded;},
 claim_wifi:()=>{if(radio.pending)return false;radio.pending=true;return true;},
 wifi_execute:(next,request)=>radio.execute(next,request)});
vm.runInContext('function recover(next){'+recovery+'}',radio);
const recover=(overrides={})=>{Object.assign(radio,{setup_requested:false,next_wifi_retry:60000000,now:60000000,
 pending:false,loaded:true,mode:1,clients:false,events:[],saved:0,connected:0,web:true},overrides);
 radio.recover({wifi_ready:true,settings_revision:0,wifi_revision:0});return radio.events;};
assert(recover().includes('connect'));assert.equal(radio.next_wifi_retry,120000000);
assert(!radio.events.includes('save'),'automatic retries must not rewrite NVS');
assert(!recover({clients:true}).includes('connect'),'active setup clients keep their connection');
assert(!recover({now:59999999}).includes('connect'));
assert(!recover({setup_requested:true}).includes('connect'));
assert(!recover({pending:true}).includes('connect'));
assert(!recover({loaded:false}).includes('connect'));
assert(!recover({mode:3}).includes('connect'));
assert(recover({connected:5}).includes('setup'),'failed retry restores the portal');
// Saved-password selection must never turn an explicitly selected open AP into a secured AP.
const passwords={M3_WIFI_CONNECT:1,M3_WIFI_CONNECT_SAVED:4,wifi_open_selected:false,wifi_selected_ssid:'Saved',
 snapshot:{wifi_password_saved:true,settings:{ssid:'Saved'}},strcmp:(a,b)=>a===b?0:1};
vm.createContext(passwords);
vm.runInContext('function choose(settings){'+body('wifi_connection_action')
 .replace('bool explicit_open','let explicit_open').replaceAll('->','.').replace('!settings.pass[0]','!settings.pass.length')+'}',passwords);
assert.equal(passwords.choose({ssid:'Saved',pass:''}),4);
assert.equal(passwords.choose({ssid:'Saved',pass:'new-secret'}),1);
assert.equal(passwords.choose({ssid:'Other',pass:''}),1);
passwords.wifi_open_selected=true;assert.equal(passwords.choose({ssid:'Saved',pass:''}),1);
passwords.wifi_selected_ssid='Other';assert.equal(passwords.choose({ssid:'Saved',pass:''}),4);
passwords.snapshot.wifi_password_saved=false;assert.equal(passwords.choose({ssid:'Saved',pass:''}),1);
// Execute secret restoration from the actual C helper, with an NVS mock.
const secrets={saved:{ssid:'Saved',pass:'stored-secret'},available:true,
 network_settings_load:s=>{Object.assign(s,secrets.saved);return secrets.available;},strcmp:passwords.strcmp,
 snprintf:(s,_size,_format,value)=>{s.pass=value;},memset:s=>{for(const k of Object.keys(s))s[k]='';secrets.cleared=s;}};
vm.createContext(secrets);
vm.runInContext('function restore(settings){'+body('restore_wifi_password',runtime)
 .replace('network_settings_t saved;','let saved = {};').replace('bool found','let found')
 .replace('saved.pass[0]','saved.pass.length').replace('sizeof(settings->pass)','64')
 .replace('snprintf(settings->pass','snprintf(settings').replaceAll('->','.')
 .replaceAll('&saved','saved').replace('sizeof(saved)','0')+'}',secrets);
let candidate={ssid:'Saved',pass:'',ip:'192.168.0.50'};
assert(secrets.restore(candidate));assert.equal(candidate.pass,'stored-secret');assert.equal(candidate.ip,'192.168.0.50');
assert.equal(secrets.cleared.pass,'');assert.equal(secrets.saved.pass,'stored-secret');
candidate={ssid:'Other',pass:''};assert(!secrets.restore(candidate));assert.equal(candidate.pass,'');
secrets.available=false;assert(!secrets.restore({ssid:'Saved',pass:''}));
// Both security icons are drawn with primitives; no font glyph dependency.
const locks={GREEN:1,MUTED:2,LV_BORDER_SIDE_TOP:1,LV_BORDER_SIDE_LEFT:2,LV_BORDER_SIDE_RIGHT:4,
 lv_obj_get_child:(obj,i)=>obj.children[i],lv_color_hex:x=>x,
 lv_obj_set_pos:(obj,x,y)=>Object.assign(obj,{x,y}),
 lv_obj_set_style_border_side:(obj,sides)=>obj.sides=sides,
 lv_obj_set_style_border_color:(obj,color)=>obj.color=color,lv_obj_set_style_bg_color:(obj,color)=>obj.color=color};
vm.createContext(locks);vm.runInContext('function security(icon,open){'+body('wifi_security')
 .replace('uint32_t color','let color').replace('lv_obj_t *shackle','let shackle')+'}',locks);
const lock={children:[{},{}]};locks.security(lock,false);assert.equal(lock.children[0].sides,7);
assert.equal(lock.children[1].color,2);locks.security(lock,true);assert.equal(lock.children[0].sides,3);
assert.equal(lock.children[0].x,10);assert.equal(lock.children[1].color,1);
assert(runtime.includes('state.initialized && state.wifi_ready'),'failed radio init must disable commands');
// Catch misspelled/nonexistent LVGL APIs against the pinned local headers.
const headers=[];function walk(dir){for(const entry of fs.readdirSync(dir,{withFileTypes:true})){
 const p=path.join(dir,entry.name);if(entry.isDirectory())walk(p);else if(p.endsWith('.h'))headers.push(fs.readFileSync(p,'utf8'));
}}
const sdk=path.join(root,'managed_components/lvgl__lvgl/src');
if(fs.existsSync(sdk)){
 walk(sdk);const declarations=headers.join('\n');
 for(const name of new Set([...ui.matchAll(/\b(lv_\w+)\s*\(/g)].map(m=>m[1])))
  assert(new RegExp('\\b'+name+'\\s*\\(').test(declarations),`not in LVGL 9.2.2 headers: ${name}`);
}
console.log('PASS: home-bar/keyboard policies, ASCII/IP entry, Wi-Fi save/fallback, saved-password reuse and wiping, open/secured icon states, timed recovery without NVS rewrites, active setup/forced setup preservation, queue admission and available LVGL declarations.');

// Execute changed C policy bodies with host objects. No native LVGL/firmware build.
const fs=require('fs'),path=require('path'),vm=require('vm'),assert=require('assert');
const root=path.resolve(__dirname,'..'),read=p=>fs.readFileSync(path.join(root,p),'utf8');
const ui=read('main/m4_ui.c'),runtime=read('main/m4_runtime.c');
function body(source,name){const m=new RegExp('\\b'+name+'\\([^;]*?\\)\\s*\\{').exec(source);assert(m,name);let start=m.index+m[0].length,i=start,d=1;while(d){if(source[i]==='{')d++;if(source[i]==='}')d--;i++;}return source.slice(start,i-1);}
const cpu={};vm.createContext(cpu);
vm.runInContext('function busy(elapsed,idle){'+body(runtime,'core_busy').replace('(unsigned)','Math.floor')+'}',cpu);
for(const [elapsed,idle,want] of [[1000000,0,100],[1000000,250000,75],[1000000,1000000,0],[1000000,1100000,0],[0,0,0]])assert.equal(cpu.busy(elapsed,idle),want);
assert(body(runtime,'log_core_load').includes('esp_ipc_call_blocking(core, read_idle_counter, &idle[core])'));
assert(body(runtime,'log_core_load').includes('1000000')&&body(runtime,'services').includes('log_core_load(&next)'));
const scan={snapshot:{scan_count:2,networks:[{ssid:'Public',authmode:0},{ssid:'Secure',authmode:1}]},
 ssid_field:{},pass_field:{},static_switch:{},wifi_info:{},WIFI_AUTH_OPEN:0,M4_WIFI_CONNECT:1,
 LV_STATE_CHECKED:1,LV_EVENT_FOCUSED:1,NULL:null,RED:1,CYAN:2,AMBER:3,T_WIFI_QUEUED:1,T_WIFI_BUSY:2,
 hide_keyboard:()=>{},lv_event_get_user_data:e=>e.index,lv_textarea_set_text:(o,s)=>o.text=s,
 lv_obj_remove_state:o=>o.checked=false,static_changed:()=>{},lv_obj_send_event:()=>scan.focused=true,
 m4_runtime_wifi:(action,settings)=>{scan.request={action,settings:{...settings}};return scan.accept;},
 memset:()=>{},tr:s=>s,set_text:(o,s)=>o.text=s,set_ink:()=>{}};
let selection=body(ui,'select_network').replace('unsigned index = (unsigned)(uintptr_t)','let index = ')
 .replace('(unsigned)snapshot.scan_count','snapshot.scan_count')
 .replace('network_settings_t settings = {.version = 1}','const settings = {version:1,pass:"",use_static:false}')
 .replace(/snprintf\(wifi_selected_ssid,[^;]+;/,'wifi_selected_ssid = snapshot.networks[index].ssid;')
 .replace(/snprintf\(settings.ssid,[^;]+;/,'settings.ssid = snapshot.networks[index].ssid;')
 .replace('bool accepted','const accepted').replace('memset(&settings, 0, sizeof(settings))','memset(settings)').replaceAll('&settings','settings');
vm.createContext(scan);vm.runInContext('function select(event){'+selection+'}',scan);
scan.accept=true;scan.select({index:0});assert.deepEqual(scan.request,{action:1,settings:{version:1,pass:'',use_static:false,ssid:'Public'}});
assert(!scan.focused,'public Wi-Fi must queue a connection without opening the keyboard');
scan.request=null;scan.select({index:1});assert(scan.focused);assert.equal(scan.request,null,'secured network waits for a password');
scan.focused=false;scan.select({index:2});assert(!scan.focused&&!scan.request);
scan.accept=false;scan.select({index:0});assert.equal(scan.wifi_info.text,scan.T_WIFI_BUSY);
// Three by three extra apps must contain every remaining app exactly once and fit the tile.
const grid={app_icons:Array.from({length:15},(_,app)=>({app,top:0,bottom:0})),APP_COUNT:16,app_names:[],TEXT:0,
 lv_button_create:parent=>{const o={parent};grid.icons.push(o);return o;},
 text:()=>({}),draw_app_icon:()=>{},launch_app:()=>{},NULL:null};
for(const name of new Set([...body(ui,'home_apps').matchAll(/\b(lv_\w+)\(/g)].map(m=>m[1])))grid[name]??=()=>{};
for(const name of new Set([...body(ui,'home_apps').matchAll(/\b[A-Z][A-Z0-9_]*\b/g)].map(m=>m[0])))grid[name]??=0;
grid.lv_obj_set_pos=(o,x,y)=>Object.assign(o,{x,y});grid.lv_obj_set_size=(o,w,h)=>Object.assign(o,{w,h});
grid.lv_obj_add_event_cb=(o,cb,event,app)=>o.app=app;
let apps=body(ui,'home_apps').replace(/_Static_assert\([^;]+;/,'')
 .replace(/sizeof\(app_icons\) \/ sizeof\(app_icons\[0\]\)/g,'app_icons.length')
 .replace(/\bunsigned |\bint /g,'let ').replace(/lv_obj_t \*/g,'let ').replace(/\(int\)|\(void \*\)\(uintptr_t\)/g,'');
apps=apps.replace('(i - first) / 3','Math.trunc((i - first) / 3)');
vm.createContext(grid);vm.runInContext('function apps(home,first,count,top){'+apps+'}',grid);
grid.icons=[];grid.apps({},6,9,76);assert.equal(grid.icons.length,9);
assert.equal(new Set(grid.icons.map(o=>o.x)).size,3);assert.equal(new Set(grid.icons.map(o=>o.y)).size,3);
assert.deepEqual(grid.icons.map(o=>o.app),[6,7,8,9,10,11,12,13,14]);
assert(grid.icons.every(o=>o.x>=0&&o.x+o.w<=480&&o.y+o.h+36<=660));
assert(ui.includes('lv_chart_set_range(cpu_chart, LV_CHART_AXIS_PRIMARY_Y, 0, 100)'));
console.log('PASS: core busy percentages, IPC sampling integration, public Wi-Fi immediate empty-password DHCP, secure/busy/bounds paths, nine unique app icons in three rows and columns.');

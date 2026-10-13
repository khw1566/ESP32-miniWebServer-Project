// Host policies only: no display emulation, firmware build, or network traffic.
const fs=require('fs'),path=require('path'),vm=require('vm'),assert=require('assert');
const root=path.resolve(__dirname,'..'),read=p=>fs.readFileSync(path.join(root,p),'utf8');
const ui=read('main/m3_ui.c'),runtime=read('main/m3_runtime.c'),relay=read('main/cloud_relay.c');
function body(name,source){
 const match=new RegExp('\\b'+name+'\\([^)]*\\)\\s*\\{').exec(source);assert(match,name);
 let start=match.index+match[0].length,end=start,depth=1;
 while(depth){if(source[end]==='{')depth++;if(source[end]==='}')depth--;end++;}
 return source.slice(start,end-1);
}
assert(!/animate_cube|cube\[|edges\[|sinf\(|cosf\(/.test(ui),'remove 3D test and its periodic work');
const apps=/static const app_id_t apps\[\] = \{([^}]+)\}/.exec(ui)[1].split(',').map(s=>s.trim());
assert.equal(apps.length,11);assert.equal(new Set(apps).size,11);assert(apps.includes('APP_AGENT')&&apps.includes('APP_TUBE')&&apps.includes('APP_MUSIC'));
assert(apps.includes('APP_WIFI')&&apps.includes('APP_TTS')&&apps.includes('APP_GAMES'));
assert(ui.includes('lv_tileview_create(home)'),'launcher supports horizontal tiles');
assert(ui.includes('int x = 42 + (int)((i - first) % 3) * 148, y = top + (int)((i - first) / 3) * 148;'));
assert(ui.includes('lv_tileview_set_tile_by_index(home_tiles, 1, 0, LV_ANIM_OFF)'),'default tile is main');
assert(ui.includes('HOME_APPS_PER_PAGE = 6'));
const icons=[...ui.matchAll(/\{(APP_\w+), 0x[0-9a-f]+, 0x[0-9a-f]+, /g)].map(m=>m[1]);
assert.deepEqual(icons,apps,'icon table follows launcher order');
for(let i=0;i<apps.length;i++){
 const local=i%6,x=42+(local%3)*148,y=(i<6?296:116)+Math.floor(local/3)*148;
 assert(x-18>=0&&x+76<=480&&x-18+112<=480,'icon and caption inside screen');assert(y+84+22<=712,'caption above home strip');
}
// Execute the actual wall-clock conversion, Korean/English date and unavailable-clock fallback.
let clock=body('update_clock',ui).replace('time_t now','let now').replace('time(NULL)','epoch')
 .replace('struct tm local;','let local = {};').replaceAll('&local','local').replaceAll('&now','now')
 .replace(/static const char \*const (\w+)\[\] = \{([^}]*)\};/g,'const $1 = [$2];')
 .replace('strftime(text, size,','text = hm(')
 .replace(/snprintf\(text, size, /g,'text = format(');
const face={epoch:0,lang:0,M3_LANG_KO:0,M3_LANG_JA:2,T_HOME_NO_TIME:'no-time',status_clock:{},home_clock:{},home_date:{},
 m3_i18n_language:()=>face.lang,tr:id=>id,
 gmtime_r:(epoch,tm)=>{const d=new Date(epoch*1000);Object.assign(tm,{tm_year:d.getUTCFullYear()-1900,tm_mon:d.getUTCMonth(),
  tm_mday:d.getUTCDate(),tm_wday:d.getUTCDay(),tm_hour:d.getUTCHours(),tm_min:d.getUTCMinutes()});},
 hm:(pattern,tm)=>String(tm.tm_hour).padStart(2,'0')+':'+String(tm.tm_min).padStart(2,'0'),
 format:(pattern,...args)=>pattern.replace(/%[ds]/g,()=>String(args.shift())),
 set_text:(obj,text)=>obj.text=text};
vm.createContext(face);vm.runInContext('function clock(text, size){'+clock+'}',face);
face.clock();assert.equal(face.home_clock.text,'--:--');assert.equal(face.home_date.text,'no-time');
face.epoch=Date.parse('2026-10-13T14:59:00Z')/1000;face.clock();
assert.equal(face.home_clock.text,'23:59');assert.equal(face.status_clock.text,'23:59');assert.equal(face.home_date.text,'2026년 10월 13일 화요일');
face.epoch+=60;face.clock();assert.equal(face.home_clock.text,'00:00');assert.equal(face.home_date.text,'2026년 10월 14일 수요일');
face.lang=1;face.clock();assert.equal(face.home_date.text,'Wed, Oct 14 2026');
face.lang=2;face.clock();assert.equal(face.home_date.text,'2026年10月14日 水曜日');
// SNTP belongs to the service worker even without a cloud provisioning token.
const service={clock_started:false,NET_MODE_STA_CONNECTED:1,mode:0,calls:0,ESP_OK:0,ESP_ERR_INVALID_STATE:259,result:0,
 network_get_mode:()=>service.mode,ESP_NETIF_SNTP_DEFAULT_CONFIG:server=>({server}),
 esp_netif_sntp_init:()=>{service.calls++;return service.result;}};
vm.createContext(service);vm.runInContext('function start(){'+body('start_network_clock',runtime)
 .replace('esp_sntp_config_t config','let config').replace('esp_err_t err','let err').replace('&config','config')+'}',service);
service.start();assert.equal(service.calls,0);
service.mode=1;service.result=5;service.start();assert(!service.clock_started);
service.result=0;service.start();assert(service.clock_started);
service.start();assert.equal(service.calls,2,'initialize once after success');
assert(!relay.includes('esp_netif_sntp_init('),'relay must not initialize SNTP concurrently');
console.log('PASS: horizontal calendar/main/apps/photos launcher with 11 preserved app icons and bounds, default main tile, removed 3D timer, KST midnight/fallback with Korean and English dates, independent single-owner SNTP.');

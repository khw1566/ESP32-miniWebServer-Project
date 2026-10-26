// Execute the small actual C membership policy in JS with lwIP/SDK mocks.
// No ESP-IDF compiler is invoked; this is not a board or linker test.
const fs=require('fs'),path=require('path'),assert=require('assert'),vm=require('vm');
const root=path.resolve(__dirname,'..');
const source=fs.readFileSync(path.join(root,'main/m4_netif_guard.c'),'utf8');
const start=source.indexOf('static esp_err_t start_once_tcpip(void *ctx) {');
const end=source.indexOf('\nvoid __wrap_',start);
assert(start>=0&&end>start);
let policy=source.slice(start,end)
 .replace('static esp_err_t start_once_tcpip(void *ctx)','function start_once_tcpip(ctx)')
 .replace('start_event_t *event','let event')
 .replace(/struct netif \*(target|item)/g,'let $1')
 .replace(/->/g,'.')
 .replace(/ESP_LOGW\([\s\S]*?\);/g,'');
const calls=[],ctx={netif_list:null,ESP_OK:0,ESP_ERR_INVALID_STATE:1,
 esp_netif_get_netif_impl:n=>n.impl,
 __real_esp_netif_action_start:(n,b,id,data)=>{
  calls.push({n,b,id,data}); n.impl.next=ctx.netif_list;ctx.netif_list=n.impl;
 }};
vm.createContext(ctx);vm.runInContext(policy,ctx);
const sta={impl:{up:false,next:null}},ap={impl:{up:true,next:null}};
const event=n=>({netif:n,base:'WIFI_EVENT',id:n===sta?2:12,data:null});
assert.equal(ctx.start_once_tcpip(event(sta)),0);assert.equal(calls.length,1);
assert.equal(ctx.start_once_tcpip(event(sta)),0);assert.equal(calls.length,1,'down STA is already registered');
ctx.start_once_tcpip(event(ap));assert.equal(calls.length,2,'AP and STA have separate membership');
ctx.start_once_tcpip(event(ap));ctx.start_once_tcpip(event(sta));assert.equal(calls.length,2);
// A real STOP removes STA from lwIP; the next START must be allowed.
ap.impl.next=null;ctx.start_once_tcpip(event(sta));assert.equal(calls.length,3);
assert.equal(ctx.start_once_tcpip(event({impl:null})),1);assert.equal(calls.length,3);
assert(source.includes('esp_netif_tcpip_exec(start_once_tcpip, &event)'));
assert(source.includes('base != WIFI_EVENT || (id != WIFI_EVENT_STA_START && id != WIFI_EVENT_AP_START)'));
const cmake=fs.readFileSync(path.join(root,'main/CMakeLists.txt'),'utf8');
assert(cmake.includes('"m4_netif_guard.c"'));
assert(cmake.includes('"-Wl,--wrap=esp_netif_action_start"'));
assert(cmake.includes('"-Wl,--undefined=__wrap_esp_netif_action_start"'),'wrapper must be an archive extraction root');
console.log('PASS: repeated STA/AP START, disconnected STA, independent interfaces, STOP/restart, null handle and TCP/IP guard wiring.');

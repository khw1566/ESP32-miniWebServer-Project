// Execute actual Wi-Fi event helpers; no device or firmware compilation.
const fs=require('fs'),path=require('path'),vm=require('vm'),assert=require('assert/strict');
const text=fs.readFileSync(path.join(__dirname,'../main/network_manager.c'),'utf8');
function body(name){const m=new RegExp('\\b'+name+'\\([^;]*?\\)\\s*\\{').exec(text);assert(m);let i=m.index+m[0].length,start=i,d=1;while(d){if(text[i]==='{')d++;if(text[i]==='}')d--;i++;}return text.slice(start,i-1);}
const c={s_sta_connect_requested:true,s_sta_connect_inflight:false,s_sta_link_up:false,s_net_mode:0,NET_MODE_STA_CONNECTING:2,
 ESP_OK:0,ESP_ERR_WIFI_NOT_STARTED:1,ESP_ERR_TIMEOUT:2,WIFI_FAIL_BIT:2,WIFI_STA_STOPPED_BIT:8,s_wifi_event_group:{},TAG:'fixture',pdFALSE:0,
 calls:0,connectResult:0,stopResult:0,stopBits:8,failBits:0,waited:0,
 ESP_LOGI:()=>{},ESP_LOGE:()=>{},ESP_LOGD:()=>{},ESP_LOGW:()=>{},pdMS_TO_TICKS:n=>n,
 esp_wifi_connect:()=>{c.calls++;return c.connectResult;},xEventGroupSetBits:(_g,b)=>c.failBits|=b,
 xEventGroupClearBits:()=>{},esp_wifi_stop:()=>c.stopResult,
 xEventGroupWaitBits:()=>{c.waited++;assert(!c.s_sta_connect_requested,'old stop/disconnect events drain while admission is disabled');return c.stopBits;}};
vm.createContext(c);
vm.runInContext('function start(){'+body('network_sta_start_connect').replace('esp_err_t err','let err')+'}',c);
c.start();c.start();assert.equal(c.calls,1,'duplicate STA_START cannot start a second connect');
c.s_sta_connect_inflight=false;c.s_sta_link_up=true;c.start();assert.equal(c.calls,1,'connected STA is not disconnected by duplicate START');
c.s_sta_link_up=false;c.s_sta_connect_requested=false;c.start();assert.equal(c.calls,1,'setup AP never connects implicitly');
c.s_sta_connect_requested=true;c.connectResult=5;c.start();assert(!c.s_sta_connect_inflight);assert(c.failBits&2);
vm.runInContext('function stop(){'+body('network_wait_sta_stop').replace('esp_err_t stop','let stop').replace('EventBits_t stopped','let stopped')+'}',c);
c.s_sta_connect_requested=false;assert.equal(c.stop(),0);assert.equal(c.waited,1);
c.stopBits=0;assert.equal(c.stop(),2,'missing stop confirmation is retryable, not a new overlapping connect');
c.stopResult=1;c.waited=0;assert.equal(c.stop(),0);assert.equal(c.waited,0,'already stopped Wi-Fi needs no event');
c.stopResult=17;assert.equal(c.stop(),17,'driver errors propagate');
const connect=body('connect_sta_visible');assert(connect.indexOf('s_sta_connect_requested = false;')<connect.indexOf('network_wait_sta_stop()'));
assert(connect.indexOf('network_wait_sta_stop()')<connect.indexOf('s_sta_connect_requested = true;'));
console.log('PASS: actual C duplicate START suppression, connected/setup guards, failed connect cleanup, ordered stop acknowledgement, timeout/error propagation and new-attempt admission. Host mocks only.');

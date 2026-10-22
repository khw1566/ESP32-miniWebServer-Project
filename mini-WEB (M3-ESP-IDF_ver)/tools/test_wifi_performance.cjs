const fs=require('fs'),path=require('path'),vm=require('vm'),assert=require('assert');
const main=path.resolve(__dirname,'../main'),source=fs.readFileSync(path.join(main,'m3_wifi_performance.c'),'utf8');
function body(name){const m=new RegExp('\\b'+name+'\\([^;]*?\\)\\s*\\{').exec(source);assert(m,name);let i=m.index+m[0].length,start=i,d=1;while(d){if(source[i]==='{')d++;if(source[i]==='}')d--;i++;}return source.slice(start,i-1);}
function setup(initial=1,getError=false,setError=false){
 const s={ps:initial,sets:[],held:false};
 const c={lock:1,users:0,original_ps:0,changed:false,portMAX_DELAY:0,pdTRUE:1,ESP_OK:0,WIFI_PS_NONE:0,
  xSemaphoreTake:()=>{assert(!s.held);s.held=true;return 1;},xSemaphoreGive:()=>{assert(s.held);s.held=false;},
  esp_wifi_get_ps:()=>{c.original_ps=s.ps;return getError?-1:0;},
  esp_wifi_set_ps:value=>{s.sets.push(value);if(setError)return -1;s.ps=value;return 0;},
  ESP_LOGW:()=>{},esp_err_to_name:()=>''};
 vm.createContext(c);
 for(const [name,js] of [['m3_wifi_performance_acquire','acquire'],['m3_wifi_performance_release','release']]){
  let code=body(name).replace(/\b(bool|esp_err_t) /g,'let ').replaceAll('&original_ps','null');
  vm.runInContext(`function ${js}(){${code}}`,c);
 }
 return {c,s};
}
{
 const {c,s}=setup();assert(c.acquire());assert(c.acquire());assert(c.acquire());
 assert.equal(c.users,3);assert.deepEqual(s.sets,[0]);
 c.release();c.release();assert.equal(s.ps,0);c.release();assert.equal(s.ps,1);
 c.release();assert.deepEqual(s.sets,[0,1]);assert.equal(c.users,0);
}
{
 const {c,s}=setup(0);assert(c.acquire());c.release();assert.deepEqual(s.sets,[]);
}
for(const opts of [[1,true,false],[1,false,true]]){
 const {c}=setup(...opts);assert(!c.acquire());assert.equal(c.users,0);c.release();
}
for(const filename of ['m3_radio.c','m3_tube.c','cloud_relay.c']){
 const code=fs.readFileSync(path.join(main,filename),'utf8');
 assert(code.includes('m3_wifi_performance_acquire()'));assert(code.includes('m3_wifi_performance_release()'));
 assert(!code.includes('esp_wifi_set_ps('),'all power policy users share ownership');
}
console.log('PASS: actual C Wi-Fi lease acquisition/release across radio, video and download; original power state restored only after the last user; RPC failures are non-fatal.');

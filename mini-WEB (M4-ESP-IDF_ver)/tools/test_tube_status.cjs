// Execute the actual authenticated PC probe with HTTP/JSON mocks. No C compiler.
const fs=require('node:fs'),path=require('node:path'),vm=require('node:vm'),assert=require('node:assert/strict');
const source=fs.readFileSync(path.join(__dirname,'../main/m4_tube.c'),'utf8');
function body(name){const m=new RegExp('\\b'+name+'\\([^)]*\\)\\s*\\{').exec(source);assert(m,name);let i=m.index+m[0].length,start=i,depth=1;while(depth){if(source[i]==='{')depth++;if(source[i]==='}')depth--;i++;}return source.slice(start,i-1);}
let probe=body('server_probe_execute');const end=probe.indexOf('\ndone:');
probe='try {\n'+probe.slice(0,end)+'\n} catch(e) {if(e!==FINISH) throw e;}\n'+probe.slice(end+6);
probe=probe.replace(/\b(?:bool |int |int64_t |const char \*|esp_http_client_handle_t |const cJSON \*|cJSON \*)(\w+)/g,'var $1')
 .replace('uint8_t data[512]; sink_t sink = {0};','var data=Buffer.alloc(512),sink={};')
 .replace('esp_http_client_config_t config =','var config=').replace(/\.(url|timeout_ms|buffer_size|crt_bundle_attach|event_handler|user_data) =/g,'$1:')
 .replace(/sizeof\([^)]*\)/g,'512').replaceAll('M4_TUBE_URL "','M4_TUBE_URL + "').replaceAll('&config','config').replaceAll('&sink','sink')
 .replace('(char *)data','data').replace('lease_tls(100, &owned)','(owned=lease_ok)').replaceAll('goto done;','throw FINISH;')
 .replaceAll('->','.').replaceAll('portENTER_CRITICAL(&service_lock);','').replaceAll('portEXIT_CRITICAL(&service_lock);','')
 .replace('snprintf(server_state.message, 512, "%s", message);','server_state.message=message;');
const valid={service:'m3-tube-relay',protocol:2,ok:true};
function run(status=200,data=valid,options={}){
 const c={Buffer,FINISH:{},NULL:null,M4_TUBE_URL:'https://fixture',M4_TUBE_TOKEN:'fixture-only',NET_MODE_STA_CONNECTED:1,
  server_state:{ready:true},server_checked:0,lease_ok:!options.busy,released:0,authenticated:false,
  m4_tube_configured:()=>!options.unconfigured,network_get_mode:()=>options.wifi?0:1,
  esp_crt_bundle_attach:()=>{},sink_event:()=>{},esp_http_client_init:()=>({}),esp_http_client_cleanup:()=>{},
  esp_http_client_set_header:(_h,key,value)=>{assert.equal(key,'x-m3-token');assert.equal(value,'fixture-only');c.authenticated=true;},
  http_get:(_h,sink,out)=>{const text=typeof data==='string'?data:JSON.stringify(data);sink.length=out.write(text);return status;},
  cJSON_Parse:buffer=>{try{return JSON.parse(buffer.subarray(0,buffer.indexOf(0)).toString());}catch{return null;}},
  cJSON_GetObjectItem:(root,key)=>{const value=root?.[key];return typeof value==='string'?{valuestring:value}:typeof value==='number'?{valueint:value}:value;},
  cJSON_IsString:v=>!!v&&typeof v.valuestring==='string',cJSON_IsNumber:v=>!!v&&typeof v.valueint==='number',
  cJSON_IsTrue:v=>v===true,cJSON_Delete:()=>{},strcmp:(a,b)=>a===b?0:1,
  https_memory_exclusive_end:()=>c.released++,esp_timer_get_time:()=>1000000,
 };
 vm.createContext(c);vm.runInContext('function probe(){'+probe+'}',c);c.probe();
 assert(!c.server_state.checking);assert.equal(c.server_checked,1000000);return c;
}
let c=run();assert(c.server_state.ready);assert(c.authenticated);assert.equal(c.released,1);
for(const status of [-1,-2,401,404,502,503,530]){c=run(status);assert(!c.server_state.ready);assert.equal(c.server_state.http_status,status);}
for(const data of ['<html>tunnel offline</html>',{}, {...valid,service:'unrelated'}, {...valid,protocol:1}, {...valid,ok:false}]){
 c=run(200,data);assert(!c.server_state.ready);
}
for(const options of [{busy:true},{wifi:true},{unconfigured:true}]){c=run(200,valid,options);assert(!c.server_state.ready);assert(!c.authenticated);}
const ui=fs.readFileSync(path.join(__dirname,'../main/m4_ui.c'),'utf8');
assert(ui.includes('server.age_s > 45'),'stale health must not remain online');
assert(source.includes('now - server_requested >= 15000000'),'UI probes must be throttled');
assert(source.includes('xQueueReceive(list_requests, &request, pdMS_TO_TICKS(500))'),'probe works before the first search');
console.log('PASS: actual C PC probe: auth, protocol/service identity, malformed replies, TLS contention, Wi-Fi, 401/404/502/503/530 and stale-state policy. Host mocks only.');

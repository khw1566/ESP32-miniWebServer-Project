// Execute the actual download-worker control flow with HTTP/SD mocks, no compiler.
const fs=require('node:fs'),path=require('node:path'),vm=require('node:vm'),assert=require('node:assert/strict');
const source=fs.readFileSync(path.join(__dirname,'../main/m3_tube_download.inc'),'utf8');
function body(name){const m=new RegExp('\\b'+name+'\\([^)]*\\)\\s*\\{').exec(source);assert(m,name);let i=m.index+m[0].length,start=i,depth=1;while(depth){if(source[i]==='{')depth++;if(source[i]==='}')depth--;i++;}return source.slice(start,i-1);}
let worker=body('download_worker');
const split=worker.indexOf('\ndone:');assert(split>0);
worker='try {\n'+worker.slice(0,split)+'\n} catch (e) { if (e !== FINISH) throw e; }\n'+worker.slice(split+6);
// C locals belong to the function, so hoist them outside the simulated goto block.
worker=worker.replace('m3_relay_profile_t relay = {0};','var relay={};').replaceAll('&relay','relay').replace(/\b(?:const char \*|FILE \*|esp_http_client_handle_t |uint8_t \*|bool |uint32_t |int64_t |int |unsigned |cJSON \*|const cJSON \*)(\w+)/g,'var $1')
 .replace('(void)unused;','')
 .replace('char url[320], job[33] = "", filename[64], final[160], partial[168];','var url="", job="", filename="", final="", partial="";')
 .replace('sink_t sink = {0};','var sink = {};').replace('struct stat existing;','var existing={};')
 .replace('uint64_t capacity, free_bytes;','var capacity=0, free_bytes=0;')
 .replace('esp_http_client_config_t config =','var config =').replace(/\.(url|timeout_ms|buffer_size|crt_bundle_attach|event_handler|user_data|disable_auto_redirect) =/g,'$1:')
 .replaceAll('M3_TUBE_URL "','M3_TUBE_URL + "').replaceAll('SD_MEDIA_ROOT M3_TUBE_DOWNLOAD_FOLDER','SD_MEDIA_ROOT + M3_TUBE_DOWNLOAD_FOLDER')
 .replaceAll('M3_TUBE_DOWNLOAD_FOLDER "','M3_TUBE_DOWNLOAD_FOLDER + "')
 .replace(/sizeof\([^)]*\)/g,'1024').replace(/\((?:char \*|uint32_t|uint64_t|size_t|unsigned)\)/g,'')
 .replace(/\b(\d+)LL\b/g,'$1').replaceAll('goto done;','throw FINISH;')
 .replaceAll('&sink','sink').replaceAll('&existing','existing').replaceAll('&config','config').replaceAll('->','.')
 .replace('lease_tls(1500, &owned)','(owned = lease_ok)')
 .replace('esp_vfs_fat_info(SD_MEDIA_ROOT, &capacity, &free_bytes)','(free_bytes = freeSpace, fat_result)')
 .replaceAll('snprintf(download_state.filename, 1024, "%s", filename);','download_state.filename = filename;')
 .replace(/snprintf\((\w+), 1024, ([^;]+)\);/g,'$1 = format($2);')
 .replaceAll('portENTER_CRITICAL(&service_lock);','').replaceAll('portEXIT_CRITICAL(&service_lock);','');
function environment(fail={}){
 const files=new Map(),state={busy:true,saved:false,filename:''};let nextRead=0;
 const c={Buffer,FINISH:{},NULL:null,ESP_OK:0,ESP_ERR_HTTP_EAGAIN:11,EEXIST:17,errno:0,
  MALLOC_CAP_SPIRAM:1,MALLOC_CAP_8BIT:2,NET_MODE_STA_CONNECTED:1,HTTP_METHOD_POST:1,HTTP_METHOD_GET:2,HTTP_METHOD_DELETE:3,
  O_WRONLY:1,O_CREAT:2,O_EXCL:4,TAG:'test',M3_TUBE_URL:'http://loopback',M3_TUBE_TOKEN:'fixture-only',SD_MEDIA_ROOT:'/sdcard',M3_TUBE_DOWNLOAD_FOLDER:'/youtube',
  download_id:'aaaaaaaaaaa',download_title:'',download_quality:0,download_state:state,files,
  lease_ok:!fail.lease,fat_result:fail.fat?-1:0,freeSpace:fail.space?1:100000000,now:1,deletedJobs:0,unlocked:0,leasedEnd:0,changed:0,
  closed:0,renamed:0,freed:0,taskDeleted:0,cancel:false,reads:0,polls:0,writes:0,
  format:(fmt,...args)=>{let i=0;return fmt.replace(/%(?:\.(\d+))?([sud])/g,(_m,n,t)=>{const v=args[i++];return t==='s'?(n?String(v).slice(0,Number(n)):String(v)):String(v);});},
  m3_relay_select:r=>{Object.assign(r,{url:'https://fixture',token:'fixture-only'});return !fail.noRelay;},quality_name:()=> 'low',sd_media_ready:()=>!fail.noSd,network_get_mode:()=>fail.wifi?0:1,
  heap_caps_malloc:()=>fail.memory?null:Buffer.alloc(8192),free:()=>c.freed++,
  esp_timer_get_time:()=>c.now,pdMS_TO_TICKS:x=>x,vTaskDelay:()=>{c.now+=100000;if(fail.cancelPrep)c.cancel=true;},
  download_stopped:()=>c.cancel,esp_crt_bundle_attach:()=>{},sink_event:()=>{},
  esp_http_client_init:()=>fail.init?null:{},esp_http_client_set_header:()=>{},esp_http_client_delete_header:()=>{},
  esp_http_client_set_method:()=>{},esp_http_client_set_url:()=>{},esp_http_client_close:()=>{},esp_http_client_cleanup:()=>{},
  download_json:(_http,sink,buf,_url,method)=>{
   if(method===3){c.deletedJobs++;return 200;}
   if(method===1){const s=JSON.stringify({job:fail.badJob?'../bad':'a'.repeat(32)});buf.write(s);sink.length=Buffer.byteLength(s);return fail.admission||202;}
   c.polls++;const s=JSON.stringify({state:fail.prep?'error':fail.cancelPrep?'preparing':'ready',error:'fixture failure'});
   buf.write(s);sink.length=Buffer.byteLength(s);return 200;
  },
  cJSON_Parse:buf=>JSON.parse(buf.subarray(0,buf.indexOf(0)).toString()),
  cJSON_GetObjectItem:(root,key)=>root&&typeof root[key]==='string'?{valuestring:root[key]}:null,
  cJSON_IsString:v=>!!v&&typeof v.valuestring==='string',cJSON_Delete:()=>{},
  strlen:s=>s.length,strspn:(s,valid)=>{let i=0;while(i<s.length&&valid.includes(s[i]))i++;return i;},strcmp:(a,b)=>a===b?0:1,
  ESP_LOGW:()=>{},sd_media_admin_lock:()=>!fail.lock,sd_media_admin_unlock:()=>c.unlocked++,
  mkdir:()=>fail.mkdir?-1:0,stat:()=>fail.existing?0:-1,
  esp_http_client_open:()=>fail.connect?-1:0,esp_http_client_fetch_headers:()=>128,esp_http_client_get_status_code:()=>200,
  esp_http_client_get_content_length:()=>fail.size||128,esp_http_client_is_complete_data_received:()=>!!fail.truncated,
  esp_http_client_read:(_http,buf,length)=>{c.reads++;if(fail.truncated&&nextRead)return 0;const n=Math.min(length,64);buf.fill(1,0,n);nextRead+=n;if(fail.cancelRead)c.cancel=true;return n;},
  unlink:name=>{files.delete(name);return 0;},open:name=>{files.set(name,Buffer.alloc(0));return {name};},
  fdopen:fd=>fail.fdopen?null:fd,close:()=>c.closed++,
  fwrite:(buf,_size,n,file)=>{c.writes++;if(fail.write)return 0;files.set(file.name,Buffer.concat([files.get(file.name),buf.subarray(0,n)]));return n;},
  fflush:()=>fail.flush?-1:0,fsync:()=>fail.sync?-1:0,fileno:f=>f,fclose:()=>{c.closed++;return fail.close?-1:0;},
  rename:(a,b)=>{c.renamed++;if(fail.rename)return -1;files.set(b,files.get(a));files.delete(a);return 0;},
  sd_media_changed:()=>c.changed++,https_memory_exclusive_end:()=>c.leasedEnd++,vTaskDelete:()=>c.taskDeleted++,
  download_progress:(message,bytes,total)=>Object.assign(state,{message,bytes,total}),
 };
 vm.createContext(c);vm.runInContext('function run(){'+worker+'}',c);return c;
}
let c=environment();c.run();assert(c.download_state.saved);assert(!c.download_state.busy);assert.equal(c.download_state.bytes,128);
assert.equal(c.download_state.filename,'aaaaaaaaaaa_low.mp4');assert.equal(c.files.size,1);assert.equal(c.renamed,1);assert.equal(c.closed,1);
assert.equal(c.unlocked,1);assert.equal(c.leasedEnd,1);assert.equal(c.changed,1);assert.equal(c.deletedJobs,0);
for(const fail of [{noSd:true},{wifi:true},{lease:true},{memory:true},{init:true},{admission:401},{admission:409},{badJob:true},
 {prep:true},{cancelPrep:true},{lock:true},{mkdir:true},{connect:true},{size:11},{size:536870913},{space:true},{fat:true},
 {fdopen:true},{write:true},{truncated:true},{cancelRead:true},{flush:true},{sync:true},{close:true},{rename:true}]){
 c=environment(fail);c.run();assert(!c.download_state.saved,JSON.stringify(fail));assert(!c.download_state.busy,JSON.stringify(fail));
 assert.equal(c.files.size,0,'no incomplete MP4 remains: '+JSON.stringify(fail));assert.equal(c.changed,0);assert.equal(c.taskDeleted,1);
}
c=environment({existing:true});c.run();assert(c.download_state.saved);assert.equal(c.writes,0);assert.equal(c.renamed,0);
console.log('PASS: actual C download worker success, 25 HTTP/SD/cancellation failure paths, partial cleanup, commit-before-publish, no overwrite and ownership release. Host mocks only.');

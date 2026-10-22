// Execute the actual C stream write/aliveness bodies with clocks and SDK mocks.
// These checks are not target compilation, RF measurements or TLS integration tests.
const fs=require('fs'),path=require('path'),vm=require('vm'),assert=require('assert');
const main=path.resolve(__dirname,'../main'),source=fs.readFileSync(path.join(main,'cloud_relay_stream.inc'),'utf8');
function body(name){const m=new RegExp('\\b'+name+'\\([^;]*?\\)\\s*\\{').exec(source);assert(m,name);let i=m.index+m[0].length,start=i,n=1;while(n){if(source[i]==='{')n++;if(source[i]==='}')n--;i++;}return source.slice(start,i-1);}
function setup({pendingAt=Infinity,disconnectAt=Infinity,stopAt=Infinity,stall=false,error=false,partial=3}={}){
 const s={us:0,cancelled:0,writes:[],idle:{value:15000000},last:0};
 const c={RELAY_STREAM_IDLE_US:15000000,NET_MODE_STA_CONNECTED:3,ESP_ERR_HTTP_EAGAIN:11,
  esp_timer_get_time:()=>s.us,https_memory_exclusive_pending:()=>s.us>=pendingAt,
  network_get_mode:()=>s.us>=disconnectAt?0:3,web_server_is_running:()=>s.us<stopAt,
  esp_http_client_cancel_request:()=>s.cancelled++,pdMS_TO_TICKS:n=>n,vTaskDelay:n=>s.us+=n*1000,
  atomic_store:(key,value)=>{s.last=value;},last_ok_seconds:'last',
  esp_http_client_write:(client,data,n)=>{s.us+=250000;if(error)return -1;if(stall)return -11;
    const count=Math.min(n,partial);s.writes.push(data.slice(0,count));return count;}};
 let alive=body('relay_stream_alive').replace(/\bint64_t /g,'let ');
 let write=body('relay_stream_write').replace(/\b(size_t|int) /g,'let ').replaceAll('(size_t)','')
  .replace('(const char *)data + sent','data.slice(sent)').replaceAll('*idle','idle.value').replaceAll('(uint32_t)','').replaceAll('&last_ok_seconds','last_ok_seconds');
 vm.createContext(c);vm.runInContext(`function relay_stream_alive(cloud,idle,maximum){${alive}}\nfunction write(cloud,data,count,idle,maximum){${write}}`,c);
 return {c,s};
}
{
 const {c,s}=setup();assert(c.write('cloud','0123456789',10,s.idle,600000000));
 assert.equal(s.writes.join(''),'0123456789');assert(s.idle.value>15000000);assert.equal(s.cancelled,0);
}
for(const options of [{pendingAt:500000},{disconnectAt:500000},{stopAt:500000},{stall:true}]){
 const {c,s}=setup(options);assert(!c.write('cloud','abcdefgh',8,s.idle,600000000));assert.equal(s.cancelled,1);
 if(options.stall)assert(s.us>=15000000&&s.us<16000000,'idle timeout remains bounded');
 else assert(s.us<1000000,'speech/Wi-Fi/server stop interrupts the next bounded write');
}
{
 const {c,s}=setup({partial:1});assert(!c.write('cloud','abcdefgh',8,s.idle,600000));assert(s.us<1000000);
}
{const {c,s}=setup({error:true});assert(!c.write('cloud','a',1,s.idle,600000000));}
assert(source.includes('MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT'));
assert(source.includes('if (!ok) esp_http_client_close(cloud);'));
assert(source.includes('free(buffer);'));
const relay=fs.readFileSync(path.join(main,'cloud_relay.c'),'utf8');
assert(relay.includes('"X-Relay-Capabilities", "stream-v2"'));
assert(relay.includes('streaming ? RELAY_STREAM_LIMIT : 8U*1024U*1024U'));
assert(source.indexOf('esp_http_client_fetch_headers')>source.indexOf('while (stream->offset < stream->length)'));
// Run the complete stream transaction as well, including allocation and cleanup.
for(const mode of ['ok','allocation','read','write','headers','ack','speech']){
 const {c,s}=setup({error:mode==='write',pendingAt:mode==='speech'?0:Infinity});
 const buffer={text:'',slice(n){return this.text.slice(n);}},stream={length:9,offset:0,status:200,mime:'text/plain',content_range:'',file:true};
 Object.assign(c,{RELAY_STREAM_BUFFER_SIZE:16384,RELAY_STREAM_LIMIT:67108864,RELAY_STREAM_MAX_US:600000000,
  MALLOC_CAP_SPIRAM:1,MALLOC_CAP_8BIT:2,HTTP_METHOD_POST:1,ESP_OK:0,NULL:null,
  CONFIG_MINI_WEB_RELAY_URL:'https://relay.example.test',device_id:'m3-fixture',
  heap_caps_malloc:()=>mode==='allocation'?null:buffer,
  free:b=>{assert.equal(b,buffer);s.freed=true;},
  snprintf:(out,size,format,...args)=>{let i=0;out.text=format.replace(/%[sd]/g,()=>String(args[i++]));return out.text.length;},
  memset:(reply)=>{reply.overflow=false;},
  esp_http_client_set_url:()=>0,esp_http_client_set_method:()=>0,esp_http_client_set_post_field:()=>0,
  esp_http_client_set_header:()=>0,esp_http_client_delete_header:()=>0,esp_http_client_set_timeout_ms:()=>0,
  relay_stream_write:c.write,
  esp_http_client_open:(client,len)=>{s.openLength=len;return 0;},
  web_server_public_read:(st,b,n)=>{if(mode==='read')return 0;const amount=Math.min(st.length-st.offset,n);b.text='abcdefghi'.slice(st.offset,st.offset+amount);st.offset+=amount;return amount;},
  esp_http_client_fetch_headers:()=>{s.headerAfterBytes=s.writes.join('').length;return mode==='headers'?-1:0;},
  esp_http_client_is_complete_data_received:()=>true,esp_http_client_get_status_code:()=>mode==='ack'?400:204,
  esp_http_client_close:()=>{s.closed=true;},ESP_LOGI:()=>{}});
 const raw=body('relay_stream_send'),split=raw.indexOf('\ndone:');assert(split>0);
 let code='sendBlock: {'+raw.slice(0,split).replaceAll('goto done;','break sendBlock;')+'}\n'+raw.slice(split+6);
 code=code.replace(/\buint8_t \*buffer/g,'var buffer').replace(/\b(int64_t|size_t|bool|int) /g,'var ')
  .replace('char url[256], status[12];','var url={},status={};').replace('char discard[128];','var discard={};')
  .replaceAll('sizeof(url)','256').replaceAll('sizeof(status)','12').replaceAll('sizeof(*reply)','0').replaceAll('sizeof(discard)','128')
  .replaceAll('stream->','stream.').replaceAll('reply->','reply.').replaceAll('idle = started + RELAY_STREAM_IDLE_US','idle = {value:started + RELAY_STREAM_IDLE_US}')
  .replaceAll('cloud,idle,maximum','cloud,idle.value,maximum').replaceAll('&idle','idle')
  .replace(/\((?:unsigned long long|unsigned|uint64_t|size_t|int)\)/g,'');
 vm.runInContext('function transaction(cloud,reply,id,stream){'+code+'}',c);
 assert.equal(c.transaction('cloud',{},'fixture-job',stream),mode==='ok',mode);
 if(mode!=='allocation')assert(s.freed,mode+' frees PSRAM buffer');
 if(mode==='ok'){assert.equal(s.openLength,9);assert.equal(s.headerAfterBytes,9);assert.equal(s.closed,undefined);}
 else if(mode!=='allocation')assert(s.closed,mode+' closes partial/failed HTTP connection');
}
console.log('PASS: actual C stream partial writes, progress/idle/overall deadlines, speech/disconnect/stop cancellation; PSRAM ownership and v1 fallback wiring (host mocks).');

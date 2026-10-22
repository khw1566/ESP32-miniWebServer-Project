// Execute actual relay streaming C bodies with SDK/clock mocks. No compiler/network/hardware.
const fs=require('fs'),path=require('path'),vm=require('vm'),assert=require('assert');
const source=fs.readFileSync(path.join(__dirname,'../main/cloud_relay.c'),'utf8').replace(/\r\n/g,'\n');
function body(name){const m=new RegExp('\\b'+name+'\\([^;]*?\\)\\s*\\{').exec(source);assert(m,name);let i=m.index+m[0].length,start=i,n=1;while(n){if(source[i]==='{')n++;if(source[i]==='}')n--;i++;}return source.slice(start,i-1);}
function setup({pendingAt=Infinity,headersAt=20000,bodyAt=0,overflow=false,openError=false,status=204,writeChunk=2}={}){
 const s={ms:0,cancelled:0,writes:[],reads:0,timeouts:[],complete:false,reply:{overflow:false},owner:'relay'};
 const c={ESP_OK:0,ESP_ERR_HTTP_EAGAIN:11,
  esp_timer_get_time:()=>s.ms*1000,https_memory_exclusive_pending:()=>s.ms>=pendingAt,
  esp_http_client_cancel_request:client=>{assert.equal(client,'relay');s.cancelled++;},
  esp_http_client_set_timeout_ms:(client,n)=>s.timeouts.push(n),
  esp_http_client_open:()=>openError?-1:0,
  esp_http_client_write:(client,data,n)=>{const count=Math.min(n,writeChunk);s.writes.push(data.slice(0,count));return count;},
  esp_http_client_fetch_headers:()=>{if(s.ms<headersAt){s.ms+=250;return -11;}return 0;},
  esp_http_client_read:()=>{s.reads++;if(s.ms<bodyAt){s.ms+=250;return -11;}s.complete=true;s.reply.overflow=overflow;return s.reads<2?2:0;},
  esp_http_client_is_complete_data_received:()=>s.complete,
  esp_http_client_get_status_code:()=>status,pdMS_TO_TICKS:n=>n,vTaskDelay:n=>{s.ms+=n;}};
 vm.createContext(c);
 let alive=body('exchange_alive');
 let perform=body('perform_interruptible').replace(/\b(int64_t|size_t|int)\s+/g,'let ')
  .replace('(const char *)data + sent','data.slice(sent)').replaceAll('(size_t)','')
  .replace('char discard[512];','let discard={};').replace('sizeof(discard)','512').replaceAll('reply->','reply.');
 vm.runInContext('function exchange_alive(client,deadline){'+alive+'}\nfunction perform(client,reply,data,length){'+perform+'}',c);
 return {c,s};
}
{
 const {c,s}=setup({pendingAt:500});assert.equal(c.perform('relay',s.reply,'',0),-1);
 assert.equal(s.cancelled,1);assert(s.ms<1000,'speech interrupts a 20 s poll within the next bounded read');
}
{
 const {c,s}=setup();assert.equal(c.perform('relay',s.reply,'',0),204);
 assert(s.ms>=20000&&s.ms<21000,'idle long polling still works');assert.equal(s.cancelled,0);
 assert.deepEqual(s.timeouts,[3000,250]);
}
{
 const {c,s}=setup({headersAt:0,bodyAt:1200,status:200});assert.equal(c.perform('relay',s.reply,'abcdef',6),200);
 assert.equal(s.writes.join(''),'abcdef','partial writes preserve the complete payload');assert(s.reads>4,'body EAGAIN retries preserve the request');
}
for(const options of [{pendingAt:0},{pendingAt:600,headersAt:0,bodyAt:20000},{headersAt:60000},{openError:true},{headersAt:0,overflow:true}]){
 const {c,s}=setup(options);assert.equal(c.perform('relay',s.reply,'',0),-1);
 if(options.headersAt===60000)assert(s.ms>=30000&&s.ms<31000,'overall deadline');
}
assert(!body('exchange').includes('esp_http_client_perform('));
assert(source.includes('if (https_memory_exclusive_pending()) continue;'));
const branch=source.slice(source.indexOf('if (https_memory_exclusive_pending()) {',source.indexOf('static void relay_task')));
assert(branch.indexOf('esp_http_client_cleanup(cloud)')<branch.indexOf('https_memory_relay_leave()'),'release only after freeing TLS');
assert(source.includes('if (https_memory_exclusive_pending()) return false;'),'do not start abort work while speech waits');
console.log('PASS: actual relay C long-poll preemption, partial writes, header/body EAGAIN, overflow/error/deadline and TLS cleanup ordering (host mocks only).');

// Execute the small platform-independent C policy bodies with mocked RTOS calls.
// This is a source harness; it does not compile or run ESP32 firmware.
const fs=require('fs'),path=require('path'),vm=require('vm'),assert=require('assert');
const main=path.resolve(__dirname,'../main');
function body(source,name){
 const match=new RegExp('\\b'+name+'\\([^;]*?\\)\\s*\\{').exec(source);assert(match,name);
 const start=match.index+match[0].length;let depth=1,end=start;
 for(;depth&&end<source.length;end++){if(source[end]==='{')depth++;else if(source[end]==='}')depth--;}
 assert.equal(depth,0);return source.slice(start,end-1);
}
const policy=fs.readFileSync(path.join(main,'https_memory.c'),'utf8');
const names=['relay_enter','relay_leave','exclusive_pending','exclusive_begin','exclusive_end'];
const functions=names.map(short=>{
 const name='https_memory_'+short;
 const code=body(policy,name).replace(/port(?:ENTER|EXIT)_CRITICAL\(&guard\);/g,'')
  .replace(/\b(?:bool|TickType_t)\s+(\w+)/g,'let $1')
  .replace(/\(TickType_t\)\(xTaskGetTickCount\(\) - start\)/g,'((xTaskGetTickCount() - start) >>> 0)');
 return `function ${name}(${short==='exclusive_begin'?'timeout_ms':''}){${code}}`;
}).join('\n');
function setup(tick=0){
 const state={tick,delay:null};
 const ctx={pdMS_TO_TICKS:x=>x,xTaskGetTickCount:()=>state.tick,
  vTaskDelay:n=>{state.tick=(state.tick+n)>>>0;if(state.delay)state.delay();}};
 vm.createContext(ctx);vm.runInContext('let relay_active=false,exclusive=false;'+functions,ctx);
 return {ctx,state};
}
{
 const {ctx:c,state:s}=setup();
 assert(c.https_memory_relay_enter());assert(!c.https_memory_relay_enter());
 s.delay=()=>{assert(c.https_memory_exclusive_pending());assert(!c.https_memory_relay_enter());if(s.tick>=100)c.https_memory_relay_leave();};
 assert(c.https_memory_exclusive_begin(1000));assert.equal(s.tick,100);
 assert(!c.https_memory_relay_enter());assert(!c.https_memory_exclusive_begin(10));
 assert(c.https_memory_exclusive_pending(),'a competing request cannot clear the owner');
 c.https_memory_exclusive_end();assert(c.https_memory_relay_enter());
}
for(const tick of [0,0xfffffff0]){
 const {ctx:c,state:s}=setup(tick);assert(c.https_memory_relay_enter());
 assert(!c.https_memory_exclusive_begin(100));assert.equal((s.tick-tick)>>>0,100);
 assert(!c.https_memory_exclusive_pending());assert(!c.https_memory_relay_enter(),'timing out does not release a live relay');
 c.https_memory_relay_leave();assert(c.https_memory_exclusive_begin(100));
 assert(!c.https_memory_relay_enter());c.https_memory_exclusive_end();assert(c.https_memory_relay_enter());
}
const tools=fs.readFileSync(path.join(main,'web_agent_tools.inc'),'utf8');
const escapeCode=body(tools,'agent_json_escape').replace(/const char \*hex/g,'const hex')
 .replace(/'(?:\\.|[^'\\])'/g,literal=>String(vm.runInNewContext(literal).charCodeAt(0)));
const escape=vm.runInNewContext(`(function(ch,out){${escapeCode}})`);
function escapedBuffer(input){const output=[];for(const ch of input){const out=[];const n=escape(ch,out);assert(n>=1&&n<=6);for(const byte of out.slice(0,n))output.push(typeof byte==='string'?byte.charCodeAt(0):byte);}return Buffer.from(output);}
for(let ch=0;ch<128;ch++)assert.equal(JSON.parse('"'+escapedBuffer([ch]).toString('utf8')+'"'),String.fromCharCode(ch));
for(let ch=128;ch<256;ch++)assert.deepEqual(escapedBuffer([ch]),Buffer.from([ch]));
const file='태평양 전쟁\n"제목"\\ 경로\t\r\u0001'.repeat(1024),wire=escapedBuffer(Buffer.from(file));
assert.equal(JSON.parse('"'+wire.toString('utf8')+'"'),file);
const html=fs.readFileSync(path.join(main,'ai_studio.html'),'utf8');
// The complete tools and diagnostics must fit the parser's structural limit.
const toolDecl=html.match(/const tools=(\[[\s\S]*?\n\]);/);assert(toolDecl);
const strDefinition=html.match(/const str=[^;]+;/);assert(strDefinition);
const aiTools=vm.runInNewContext(strDefinition[0]+toolDecl[0]+'tools');
const payload=JSON.stringify({system_instruction:{parts:[{text:'Instructions'}]},contents:[{role:'user',parts:[{text:'보드 상태를 확인해줘'}]}],tools:[{functionDeclarations:aiTools}],generationConfig:{temperature:0.4,maxOutputTokens:8192}});
let nodes=1,quoted=false,escaped=false;
for(const ch of payload){if(quoted){if(escaped)escaped=false;else if(ch==='\\')escaped=true;else if(ch==='"')quoted=false;}else if(ch==='"')quoted=true;else if('{[:,'.includes(ch))nodes++;}
assert(nodes<=384,`complete AI tool schema needs ${nodes} structural slots`);
for(const config of ['../sdkconfig.defaults']){
 const content=fs.readFileSync(path.resolve(__dirname,config),'utf8');
 assert(content.includes('CONFIG_SPIRAM_MODE_HEX=y'));assert(content.includes('CONFIG_SPIRAM_USE_MALLOC=y'));assert(content.includes('CONFIG_MBEDTLS_SSL_OUT_CONTENT_LEN=8192'));
 assert(content.includes('CONFIG_MBEDTLS_SSL_IN_CONTENT_LEN=16384'));
}
console.log('Memory policy: exclusive admission, relay yield, timeout/retry and tick wrap passed.');
console.log('File streaming: all control bytes, Unicode, quotes and large content round-trip passed.');
console.log(`Complete AI tool schema: ${nodes}/384 slots; PSRAM/TLS configurations agree.`);

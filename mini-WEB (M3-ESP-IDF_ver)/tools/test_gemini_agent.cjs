// Execute the C action dispatcher and text/key policies with mocks. No cloud calls.
const fs=require('fs'),path=require('path'),vm=require('vm'),assert=require('assert');
const root=path.resolve(__dirname,'..'),read=p=>fs.readFileSync(path.join(root,p),'utf8');
const source=read('main/m3_agent.c'),ui=read('main/m3_ui.c');
function body(name){const m=new RegExp('\\b'+name+'\\([^)]*\\)\\s*\\{').exec(source);assert(m,name);
 let start=m.index+m[0].length,end=start,depth=1;while(depth){if(source[end]==='{')depth++;if(source[end]==='}')depth--;end++;}return source.slice(start,end-1);}
const declaration=source.slice(source.indexOf('static const char declarations[] =')).split(/;\n/)[0];
const schema=JSON.parse([...declaration.matchAll(/"(?:\\.|[^"\\])*"/g)].map(m=>JSON.parse(m[0])).join(''));
assert.deepEqual(schema.map(s=>s.name),['get_device_status','set_volume','stop_audio','open_app','speak_text']);
assert.equal(schema[1].parameters.properties.percent.maximum,150);
assert.equal(schema[3].parameters.properties.app.enum.length,12);
const models=/static const char \*models\[\] = \{([^}]+)\}/.exec(source)[1].match(/"[^"]+"/g).map(s=>JSON.parse(s));
assert.deepEqual(models,['gemini-3.1-flash-lite','gemini-3.5-flash-lite']);
const actions={deferred_speech:{0:0,value:""},sizeof:()=>385,
 snprintf:(dest,capacity,format,value)=>{dest[0]=1;dest.value=value;},
 m3_tts_text_valid:text=>!!text&&Buffer.byteLength(text)<=384,token:1,events:[],open_app:-1,open_generation:0,MALLOC_CAP_INTERNAL:1,MALLOC_CAP_8BIT:2,MALLOC_CAP_SPIRAM:4,NET_MODE_STA_CONNECTED:1,NET_MODE_AP_CAPTIVE:2,
 active:token=>token===actions.token,strcmp:(a,b)=>a===b?0:1,
 cJSON_CreateObject:()=>({}),cJSON_AddBoolToObject:(o,k,v)=>o[k]=v,cJSON_AddStringToObject:(o,k,v)=>o[k]=v,
 cJSON_AddNumberToObject:(o,k,v)=>o[k]=v,cJSON_GetObjectItemCaseSensitive:(o,k)=>o?.[k]??null,
 cJSON_IsObject:o=>!!o&&typeof o==='object'&&!Array.isArray(o),
 cJSON_IsString:o=>typeof o?.valuestring==='string',cJSON_IsNumber:o=>Number.isFinite(o?.valuedouble),
 m3_runtime_state:o=>Object.assign(o,{mode:1,ip:'192.168.1.40',rssi:-60,web:true,flash:{used:1024,total:16777216,reserved:8388608}}),
 m3_audio_state:o=>Object.assign(o,{playing:false,volume:35,message:'Ready'}),m3_voice_state:o=>Object.assign(o,{busy:false}),
 heap_caps_get_free_size:()=>123456,m3_audio_volume:v=>actions.events.push(['volume',v]),
 m3_audio_stop:()=>actions.events.push(['stop']),m3_audio_tts:(text,speed)=>{
  actions.events.push(['speak',text,speed]);return Buffer.byteLength(text)<=384;}};
vm.createContext(actions);vm.runInContext('function tool_result(ok,detail){'+body('tool_result').replace('cJSON *result','let result')+'}',actions);
let execute=body('execute_tool').replace(/\b(?:m3_runtime_state_t runtime|m3_audio_state_t audio|m3_voice_state_t voice);/g,m=>'let '+m.split(' ')[1].slice(0,-1)+' = {};')
 .replace(/\bcJSON \*(\w+)/g,'let $1').replace(/\bbool (\w+)/g,'let $1').replace(/\bunsigned (\w+)/g,'let $1')
 .replaceAll('->','.').replace(/&runtime|&audio|&voice/g,m=>m.slice(1))
 .replace(/static const char \*apps\[\] = \{([^}]+)\};/,'const apps = [$1];')
 .replace('sizeof(apps) / sizeof(apps[0])','apps.length')
 .replace('atomic_store(&open_generation, token)','open_generation = token').replace('atomic_store(&open_app, i)','open_app = i');
vm.runInContext('function execute(name,args,token){'+execute+'}',actions);
const num=n=>({valuedouble:n,valueint:Math.trunc(n)}),str=s=>({valuestring:s});
for(const n of [0,35,70,100,150]){const r=actions.execute('set_volume',{percent:num(n)},1);assert(r.ok);assert.equal(r.percent,n);}
const before=actions.events.length;
for(const n of [-1,151,0.5,1e10])assert(!actions.execute('set_volume',{percent:num(n)},1).ok);
assert(!actions.execute('set_volume',{percent:str('20')},1).ok);assert.equal(actions.events.length,before);
const snapshot=actions.execute('get_device_status',null,1);assert(snapshot.ok);assert.equal(snapshot.rssi_dbm,-60);
assert(!JSON.stringify(snapshot).includes('api_key'));
for(let i=0;i<schema[3].parameters.properties.app.enum.length;i++){
 assert(actions.execute('open_app',{app:str(schema[3].parameters.properties.app.enum[i])},1).ok);assert.equal(actions.open_app,i);
}
assert(!actions.execute('open_app',{app:str('../files/delete')},1).ok);
assert(actions.execute('stop_audio',null,1).ok);
assert(actions.execute('speak_text',{text:str('안녕하세요')},1).ok);
assert.equal(actions.deferred_speech.value,'안녕하세요');
assert(!actions.events.some(e=>e[0]==='speak'),'speech must wait until agent TLS lease is released');
assert(!actions.execute('speak_text',{text:str('가'.repeat(129))},1).ok);
assert(!actions.execute('format_flash',{},1).ok);
const count=actions.events.length;actions.token=2;
for(const name of schema.map(s=>s.name))assert(!actions.execute(name,{percent:num(20),app:str('wifi'),text:str('test')},1).ok);
assert.equal(actions.events.length,count,'cancelled generation must not execute further actions');
const keys={strlen:s=>s.length,strncmp:(a,b,n)=>a.slice(0,n)===b.slice(0,n)?0:1};vm.createContext(keys);
vm.runInContext('function valid(key){'+body('key_valid').replace(/\(unsigned char\)key\[i\]/g,'key.charCodeAt(i)')
 .replace(/\bsize_t (\w+)/g,'let $1')+'}',keys);
assert(!keys.valid('gen-lang-client-0210670991'));assert(!keys.valid('short'));assert(!keys.valid('x'.repeat(256)));
assert(!keys.valid('x'.repeat(30)+'\r\nInjected'));assert(keys.valid('x'.repeat(40)));
const text={strlen:b=>{const n=b.indexOf(0);return n<0?b.length:n;},memcpy:(dest,src,n)=>src.copy(dest,0,0,n)};
vm.createContext(text);vm.runInContext('function append(buffer,capacity,text){'+body('append_text')
 .replace(/\bsize_t (\w+)/g,'let $1').replace('(unsigned char)','')
 .replace('buffer + used','buffer.subarray(used)')+'}',text);
for(let capacity=1;capacity<15;capacity++){
 const out=Buffer.alloc(capacity);text.append(out,capacity,Buffer.from('한국어 test\0'));
 const value=out.subarray(0,text.strlen(out));assert.equal(value.toString('utf8').includes('\ufffd'),false);
 assert(value.length<capacity);
}
assert(source.includes('AGENT_MAX_ROUNDS = 3'));assert(source.includes('AGENT_MAX_TOOLS = 6'));
assert(source.includes('cJSON_Duplicate(content, true)'),'preserve opaque Gemini thought signatures');
assert(source.includes('cJSON_AddStringToObject(response, "id", id->valuestring)'));
assert(source.includes('"functionResponse"'));assert(source.includes('"x-goog-api-key", m3_sd_gemini_key()'));
assert(source.includes('.disable_auto_redirect = true'));assert(source.includes('https_memory_exclusive_end()'));
assert(source.includes('request.conversation != history_generation || request.model != history_model'));
assert(ui.includes('models[] = {"Gemini 3.1 Flash-Lite", "Gemini 3.5 Flash-Lite"}'));assert(ui.includes('segment_value(agent_model)'));
assert(ui.includes('m3_agent_take_open_app()'));assert(ui.includes('job_update(5, tr(T_JOB_AGENT)'));
assert(!/\blv_\w+\(/.test(source),'network worker may not call LVGL');
console.log('PASS: exact model choices, 5 validated device tools, cancellation before actions, project-ID/key validation, UTF-8 clipping, round limits, signatures/IDs, history reset and UI dispatch.');

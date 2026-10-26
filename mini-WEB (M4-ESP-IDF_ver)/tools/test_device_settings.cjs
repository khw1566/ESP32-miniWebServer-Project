// Execute the authenticated C HTTP handler with request, JSON and persistence mocks.
const fs=require('fs'),path=require('path'),vm=require('vm'),assert=require('node:assert/strict');
const root=path.resolve(__dirname,'..'),source=fs.readFileSync(path.join(root,'main/web_device_settings.inc'),'utf8');
function body(name){const m=new RegExp('\\b'+name+'\\([^)]*\\)\\s*\\{').exec(source);assert(m,name);
 let i=m.index+m[0].length,start=i,depth=1;while(depth){if(source[i]==='{')depth++;if(source[i]==='}')depth--;i++;}return source.slice(start,i-1);}
const wrap=v=>({raw:v,valuestring:typeof v==='string'?v:undefined,valuedouble:typeof v==='number'?v:NaN});
const flat=body('device_settings_json_flat').replace('char c = input[i];','let c = String.fromCharCode(input[i]);')
 .replace(/\b(?:unsigned|bool|size_t) (\w+)/g,'let $1');
const number=body('settings_number').replace('const cJSON *value','let value').replaceAll('->','.')
 .replace('(unsigned)value.valuedouble','Math.trunc(value.valuedouble)').replace('*out = (uint8_t)value.valuedouble','out.obj[out.key] = value.valuedouble');
const post=body('device_settings_post').replace('char input[513]; size_t used = 0;','let input=new Uint8Array(513); let used=0;')
 .replace('m4_preferences_t p, before;','let p={},before={};').replace('p = before','p = {...before}')
 .replace(/&p\.(\w+)/g,'({obj:p,key:"$1"})').replaceAll('&before','before').replaceAll('&p','p')
 .replaceAll('input + used','input.subarray(used)').replaceAll('sizeof(input)','513')
 .replace(/\b(?:const cJSON \*|cJSON \*|const char \*)(\w+)/g,'let $1')
 .replace(/\b(?:int|bool|m4_mode_result_t) (\w+)/g,'let $1').replaceAll('->','.');
function call(payload,options={}){
 const bytes=Buffer.from(typeof payload==='string'?payload:JSON.stringify(payload));
 const initial={version:1,brightness:100,max_fps:30,volume:40,last_volume:40,animations:1,web_focus:0,reserved:0,...options.settings};
 const c={ESP_OK:0,M4_MODE_OK:0,M4_MODE_BUSY:1,M4_MODE_SAVE_FAILED:2,M4_MODE_RESTART_FAILED:3,
 writes:0,modes:0,volumes:[],position:0,settings:initial,
 httpd_req_recv:(_req,out,n)=>{const count=Math.min(n,17,bytes.length-c.position);out.set(bytes.subarray(c.position,c.position+count));c.position+=count;return count;},
 memset:(out,value)=>out.fill(value),strlen:x=>Buffer.byteLength(x),
 cJSON_Parse:input=>{try{return JSON.parse(Buffer.from(input).toString().split('\0')[0]);}catch{return null;}},
 cJSON_GetObjectItemCaseSensitive:(obj,k)=>wrap(obj?.[k]),cJSON_IsString:v=>typeof v.raw==='string',
 cJSON_IsNumber:v=>typeof v.raw==='number',cJSON_IsBool:v=>typeof v.raw==='boolean',cJSON_IsTrue:v=>v.raw===true,
 cJSON_Delete:()=>{},cJSON_CreateObject:()=>({}),
 cJSON_AddNumberToObject:(obj,k,v)=>obj[k]=v,cJSON_AddBoolToObject:(obj,k,v)=>obj[k]=!!v,cJSON_AddStringToObject:(obj,k,v)=>obj[k]=v,
 verify_admin_password:password=>password==='password1',
 m4_preferences_get:out=>Object.assign(out,c.settings),m4_preferences_restarting:()=>!!options.restarting,
 m4_preferences_web_active:()=>!!options.active,
 m4_preferences_save:p=>{c.writes++;if(options.saveFail)return 5;c.settings={...p};return 0;},
 m4_preferences_restart_with:p=>{c.modes++;if(!options.modeResult||options.modeResult===3)c.settings={...p};return options.modeResult??0;},
 m4_audio_volume:v=>c.volumes.push(v),setup_error:(_req,status,message)=>({status:parseInt(status),message}),
 setup_json:(_req,status,reply)=>({status:parseInt(status),reply}),httpd_resp_send_500:()=>({status:500})};
 vm.createContext(c);vm.runInContext('function device_settings_json_flat(input){'+flat+'}\nfunction settings_number(body,name,minimum,maximum,out){'+number+'}\nfunction post(req){'+post+'}',c);
 return {result:c.post({content_len:options.length??bytes.length}),c};
}
const good={cur_pw:'password1',brightness:25,max_fps:15,volume:0,animations:false,web_focus:false};
let r=call(good);assert.equal(r.result.status,200);assert.equal(r.c.settings.brightness,25);assert.equal(r.c.settings.volume,0);
assert.equal(r.c.settings.last_volume,40);assert.deepEqual(r.c.volumes,[0]);assert.equal(r.c.modes,0);
r=call({cur_pw:'password1',read:true});assert.equal(r.result.status,200);assert.equal(r.c.writes,0);assert(!('cur_pw'in r.result.reply));
for(const cur_pw of ['', 'wrong', 'x'.repeat(65)]){r=call({...good,cur_pw});assert.equal(r.result.status,403);assert.equal(r.c.writes,0);assert.equal(r.c.modes,0);}
for(const patch of [{brightness:4},{brightness:101},{brightness:12.5},{max_fps:0},{max_fps:61},{max_fps:'30'},
 {volume:151},{volume:-1},{animations:1},{web_focus:1}]){
 r=call({...good,...patch});assert.equal(r.result.status,400,JSON.stringify(patch));assert.equal(r.c.writes,0);assert.equal(r.c.modes,0);
}
for(const length of [0,513])assert.equal(call(good,{length}).result.status,400);
assert.equal(call('invalid JSON').result.status,403);
assert.equal(call({cur_pw:'password1',read:true,nested:{a:1}}).result.status,400,'nested input is rejected before recursive parsing');
r=call(good,{saveFail:true});assert.equal(r.result.status,500);assert.equal(r.c.settings.brightness,100);
r=call({...good,web_focus:true});assert.equal(r.result.status,200);assert.equal(r.c.modes,1);assert.equal(r.c.settings.web_focus,true);
assert.equal(r.c.writes,0,'mode changes use the guarded save/restart transaction');
r=call(good,{settings:{web_focus:1},active:true});assert.equal(r.result.status,200);assert.equal(r.c.modes,1,'headless mode can restore normal boot');
for(const [modeResult,status] of [[1,409],[2,500],[3,500]])assert.equal(call({...good,web_focus:true},{modeResult}).result.status,status);
assert.equal(call(good,{restarting:true}).result.status,409);
const page=[...source.split('static esp_err_t device_settings_get')[0].matchAll(/^"(?:\\.|[^"\\])*"/gm)].map(m=>JSON.parse(m[0])).join('');
assert(page.includes("type='password'"));assert(page.includes("fieldset disabled"));assert(page.includes("/api/device-settings"));
assert(page.includes("$('status').textContent"));assert(!page.includes('localStorage'));
console.log('PASS: real C HTTP control flow with administrator authentication, fragmented body reads, size/type/integer/range checks, save failure, guarded focus/normal transitions, restart admission and credential-free replies. Host mocks only.');

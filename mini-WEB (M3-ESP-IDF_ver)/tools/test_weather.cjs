// Execute bounded C parser policies with cJSON/strings mocked in JavaScript.
// This does not compile firmware or prove device HTTPS/SD/LVGL behavior.
const fs=require('fs'),path=require('path'),vm=require('vm'),assert=require('assert');
const root=path.resolve(__dirname,'..'),read=p=>fs.readFileSync(path.join(root,p),'utf8');
const source=read('main/m3_weather_json.c');
function body(name,text=source){const m=new RegExp('\\b'+name+'\\([^)]*\\)\\s*\\{').exec(text);assert(m,name);
 let start=m.index+m[0].length,end=start,depth=1;while(depth){if(text[end]==='{')depth++;if(text[end]==='}')depth--;end++;}return text.slice(start,end-1);}
const ref=(object,key)=>({get value(){return object[key]},set value(v){object[key]=v}});
const unwrap=v=>v&&typeof v==='object'&&'value'in v?v.value:v;
const wrap=raw=>raw===undefined?null:{raw,valuedouble:typeof raw==='number'?raw:0,valuestring:typeof raw==='string'?raw:null};
const blankPlace=()=>({name:'',area:'',latitude:0,longitude:0});
const blankData=()=>({temperature:0,feels_like:0,humidity:0,wind:0,code:0,time:'',days:Array.from({length:3},()=>({valid:false,date:''}))});
const c={ref,blankPlace,blankData,M3_WEATHER_DAYS:3,M3_WEATHER_RESULTS:5,NULL:null,
 isfinite:Number.isFinite,fabs:Math.abs,floor:Math.floor,strlen:s=>unwrap(s).length,
 memchr:(s,zero,n)=>s.length<n,
 snprintf:(target,capacity,format,...args)=>{let i=0;target.value=format.replace(/%s/g,()=>unwrap(args[i++])).slice(0,capacity-1);},
 cJSON_Parse:s=>{try{return wrap(JSON.parse(s))}catch{return null}},cJSON_Delete:()=>{},
 cJSON_GetObjectItemCaseSensitive:(node,key)=>wrap(node?.raw?.[key]),
 cJSON_GetArrayItem:(node,i)=>wrap(Array.isArray(node?.raw)?node.raw[i]:undefined),
 cJSON_IsNumber:n=>typeof n?.raw==='number',cJSON_IsString:n=>typeof n?.raw==='string',
 cJSON_IsObject:n=>n?.raw!==null&&typeof n?.raw==='object'&&!Array.isArray(n.raw),
 cJSON_IsArray:n=>Array.isArray(n?.raw),cJSON_IsTrue:n=>n?.raw===true,
 array:n=>(n?.raw||[]).map(wrap)};
vm.createContext(c);
function convert(name){let s=body(name).replaceAll('->','.');
 s=s.replace(/\(m3_weather_place_t\)\{0\}/g,'blankPlace()')
  .replace('m3_weather_data_t next = {0};','let next = blankData();')
  .replace('m3_weather_place_t next;','let next = blankPlace();')
  .replace('m3_weather_day_t *day = &next.days[i];','let day = next.days[i];')
  .replace('char admin[80] = "", country[40] = "";','let admin = "", country = "";')
  .replace(/\b(?:const cJSON \*|cJSON \*|bool |double |unsigned )/g,'let ')
  .replaceAll('*out','out.value').replaceAll('*count','count.value')
  .replace(/\bout\.(name|area|latitude|longitude)/g,'out.value.$1')
  .replace('let code;','let code; const codeRef={get value(){return code},set value(v){code=v}};')
  .replace('let version;','let version; const versionRef={get value(){return version},set value(v){version=v}};')
  .replaceAll('&code','codeRef').replaceAll('&version','versionRef')
  .replaceAll('&next','nextRef').replace('let next = blankPlace();','let next = blankPlace(); const nextRef={get value(){return next},set value(v){next=v}};');
 // Ampersand-to-reference conversion for numeric structure members.
 s=s.replace(/nextRef\.(\w+)/g,"ref(next,'$1')")
  .replace(/&((?:out\.value|day)\.(\w+))/g,(_,all,key)=>`ref(${all.slice(0,all.lastIndexOf('.'))},'${key}')`)
  .replace(/sizeof\((?:p|out\.value)\.(name|area)\)/g,(_,key)=>key==='name'?'96':'128')
  .replace(/sizeof\(next\.time\)/g,'32').replace(/sizeof\(day\.date\)/g,'11')
  .replace(/sizeof\(next\.area\)/g,'128').replace(/sizeof\(admin\)/g,'80').replace(/sizeof\(country\)/g,'40')
  .replace(/string\(([^,]+), ("[^"]+"), ((?:out\.value|next)\.(\w+)),/g,(_,obj,key,field,prop)=>`string(${obj}, ${key}, ref(${field.slice(0,field.lastIndexOf('.'))},'${prop}'),`)
  .replace('string(obj, "admin1", admin,','string(obj, "admin1", {get value(){return admin},set value(v){admin=v}},')
  .replace('string(obj, "country", country,','string(obj, "country", {get value(){return country},set value(v){country=v}},')
  .replace('snprintf(out.value.area,','snprintf(ref(out.value,"area"),').replace('snprintf(day.date,','snprintf(ref(day,"date"),')
  .replace('cJSON_ArrayForEach(item, results)', 'for (item of array(results))')
  .replace('m3_weather_place_valid(out)', 'm3_weather_place_valid(out.value)')
  .replace('place(item, &out[count.value])','place(item, ref(out,count.value))')
  .replace(/\(int\)code/g,'code');
 return s;}
for(const [name,args]of Object.entries({m3_weather_place_valid:'p',number:'item,low,high,out',member:'obj,key,low,high,out',string:'obj,key,out,capacity',
 place:'obj,out',m3_weather_parse_setting:'json,out',m3_weather_parse_places:'json,out,count',m3_weather_parse:'json,out'})){
 const s=convert(name);try{vm.runInContext(`function ${name}(${args}){${s}}`,c)}catch(e){throw new Error(name+'\n'+s+'\n'+e)}
}
function parse(value){const out={value:null};const ok=c.m3_weather_parse(JSON.stringify(value),out);return {ok,data:out.value};}
const fixture={current:{temperature_2m:19.5,apparent_temperature:18.2,relative_humidity_2m:61,wind_speed_10m:2.8,weather_code:2,time:'2026-10-26T10:15'},
 daily:{time:['2026-10-26','2026-10-27','2026-10-28'],temperature_2m_min:[10,11,null],temperature_2m_max:[20,21,22],precipitation_probability_max:[10,70,30],weather_code:[2,61,0]}};
let parsed=parse(fixture);assert(parsed.ok);assert.equal(parsed.data.temperature,19.5);assert(parsed.data.days[0].valid);assert(!parsed.data.days[2].valid);
for(const [key,value]of [['temperature_2m',null],['wind_speed_10m',-1],['relative_humidity_2m',101],['weather_code',2.5],['time','x'.repeat(32)]]){
 const changed=structuredClone(fixture);changed.current[key]=value;assert(!parse(changed).ok,key);}
assert(!parse({}).ok);assert(!parse({error:true}).ok);assert(!c.m3_weather_parse('{broken',{value:null}));
const noDaily=structuredClone(fixture);delete noDaily.daily;assert(parse(noDaily).ok);assert(parse(noDaily).data.days.every(d=>!d.valid));
const inverted=structuredClone(fixture);inverted.daily.temperature_2m_min[0]=40;assert(!parse(inverted).data.days[0].valid);
const setting={version:1,name:'울산',area:'Ulsan, KR',latitude:35.5384,longitude:129.3114};
let out={value:null};assert(c.m3_weather_parse_setting(JSON.stringify(setting),out));assert.equal(out.value.name,'울산');
for(const change of [{latitude:91},{longitude:-181},{name:''},{name:'x'.repeat(96)},{version:2}])assert(!c.m3_weather_parse_setting(JSON.stringify({...setting,...change}),{value:null}));
let results=Array.from({length:5},blankPlace),count={value:0};
assert(c.m3_weather_parse_places(JSON.stringify({results:[{name:'bad',latitude:100,longitude:0},...Array.from({length:8},(_,i)=>({...setting,name:'City '+i,admin1:'Region',country:'KR'}))]}),results,count));
assert.equal(count.value,5);assert.equal(results[0].area,'Region, KR');
assert(c.m3_weather_parse_places('{}',results,count));assert.equal(count.value,0);
assert(!c.m3_weather_parse_places('{"error":true}',results,count));assert(!c.m3_weather_parse_places('{"results":{}}',results,count));
// Execute the real percent encoder over UTF-8 bytes, including reserved URL syntax.
let encode=body('m3_weather_encode').replace('static const char hex[] = "0123456789ABCDEF";','const hex="0123456789ABCDEF";')
 .replace('size_t used','let used').replace('for (const unsigned char *p = (const unsigned char *)input; *p; ++p)', 'for (let i=0,p=input; i<input.length; ++i)')
 .replace(/\*p/g,'p[i]').replace(/bool plain/g,'let plain').replace(/\(char\)/g,'');
encode=encode.replace(/'([a-zA-Z0-9_.~-])'/g,(_,v)=>String(v.charCodeAt(0)));
vm.runInContext('function encode(input,out,capacity){'+encode+'}',c);
for(const query of ['울산','서울, KR','Paris &x=?#','Tokyo+日本']){
 const buffer=[];assert(c.encode(Buffer.from(query),buffer,480));
 const encoded=buffer.filter(v=>v!==0).map(v=>typeof v==='number'?String.fromCharCode(v):v).join('');assert.equal(encoded,encodeURIComponent(query));}
assert(!c.encode(Buffer.from('서울'),[],4));assert(!c.encode(Buffer.alloc(0),[],20));
const backend=read('main/m3_weather.c'),ui=read('main/m3_ui.c'),weatherUI=read('main/m3_weather_ui.inc');
const queue={state:{busy:false,status:0,revision:0},started:true,now:100,next_manual:0,restarting:false,web:false,admit:true,sent:0,
 M3_WEATHER_LOADING:1,M3_WEATHER_WAIT:8,pdPASS:1,requests:{},lock:{},
 atomic_load:x=>x,portENTER_CRITICAL:()=>{},portEXIT_CRITICAL:()=>{},esp_timer_get_time:()=>queue.now,
 m3_preferences_restarting:()=>queue.restarting,m3_preferences_web_active:()=>queue.web,
 xQueueSend:()=>{queue.sent++;return queue.admit?1:0;}};
vm.createContext(queue);
for(const [name,args]of [['finish','status'],['submit','request']])vm.runInContext(`function ${name}(${args}){`+body(name,backend)
 .replaceAll('->','.').replace(/\bbool /g,'let ').replaceAll('&started','started').replaceAll('&lock','lock')+'}',queue);
assert(queue.submit({kind:0}));assert(queue.state.busy);assert.equal(queue.sent,1);
assert(!queue.submit({kind:1}));assert.equal(queue.sent,1,'busy worker cannot accept a second location');
queue.finish(2);queue.admit=false;assert(!queue.submit({kind:1}));assert(!queue.state.busy,'queue failure releases busy state');
queue.admit=true;queue.next_manual=200;assert(!queue.submit({kind:2}));
assert(queue.submit({kind:1}),'choosing a result is allowed during search cooldown');queue.finish(2);
queue.web=true;assert(!queue.submit({kind:1}));queue.web=false;queue.restarting=true;assert(!queue.submit({kind:1}));
assert(backend.includes('.crt_bundle_attach = esp_crt_bundle_attach')&&backend.includes('.disable_auto_redirect = true'));
assert(backend.includes('MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT')&&backend.includes('length <= RESPONSE_LIMIT'));
assert(backend.includes('esp_http_client_is_complete_data_received(client)'));
assert(backend.includes('SETTINGS_PATH ".bak"')&&backend.includes('fsync(fileno(file))'));
assert(!/fopen\(|esp_http_client_|xQueueReceive/.test(weatherUI));
assert(ui.includes('lv_obj_add_event_cb(home_clock, launch_app, LV_EVENT_CLICKED, (void *)(uintptr_t)APP_WEATHER)'));
assert(ui.includes('build_weather(pages[APP_WEATHER])'));
assert(read('main/m3_runtime.c').indexOf('m3_weather_init();')>read('main/m3_runtime.c').indexOf('sd_media_init()'));
assert.equal((read('main/m3_weather_presets.inc').match(/\{"/g)||[]).length,8);
console.log('PASS: weather C parser policies: malformed/null/range/partial forecasts, settings versions/coordinates, duplicate-city context, five-result cap, UTF-8 URL encoding; HTTPS/PSRAM/SD/UI integration.');

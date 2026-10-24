// Execute the actual relay JSON validation and candidate selection with mocked HTTP.
const fs=require('fs'),path=require('path'),vm=require('vm'),assert=require('assert/strict');
const text=fs.readFileSync(path.join(__dirname,'../main/m3_relay.c'),'utf8');
function body(name){const m=new RegExp('\\b'+name+'\\([^;]*?\\)\\s*\\{').exec(text);assert(m);let i=m.index+m[0].length,start=i,d=1;while(d){if(text[i]==='{')d++;if(text[i]==='}')d--;i++;}return text.slice(start,i-1);}
const c={M3_RELAY_MAX:8,profile_count:0,selected:0,profiles:Array.from({length:8},()=>({})),NULL:null,attempts:[],online:new Set(),
 cJSON_GetObjectItemCaseSensitive:(o,k)=>{const v=o?.[k];return typeof v==='string'?{valuestring:v}:typeof v==='number'?{valuedouble:v}:v;},
 cJSON_GetArraySize:a=>Array.isArray(a)?a.length:0,cJSON_GetArrayItem:(a,i)=>a[i],
 cJSON_IsObject:v=>v!==null&&typeof v==='object'&&!Array.isArray(v),cJSON_IsArray:Array.isArray,
 cJSON_IsString:v=>!!v&&typeof v.valuestring==='string',cJSON_IsNumber:v=>!!v&&typeof v.valuedouble==='number',
 strlen:v=>v.length,strncmp:(a,b,n)=>a.slice(0,n)===b.slice(0,n)?0:1,
 probe:p=>{c.attempts.push(p.token);return c.online.has(p.token);},
 reset:()=>{c.profiles=Array.from({length:8},()=>({}));},clear:o=>{for(const k of Object.keys(o))delete o[k];}};
vm.createContext(c);
let valid=body('valid_profile').replace(/const cJSON \*/g,'const ').replace(/(?:const )?size_t /g,'let ')
 .replaceAll('sizeof(out->url)','192').replaceAll('sizeof(out->token)','256')
 .replace('unsigned char c = url->valuestring[i]','let c = url.valuestring.charCodeAt(i)')
 .replaceAll('(unsigned char)token->valuestring[i]','token.valuestring.charCodeAt(i)')
 .replace('snprintf(out->url, 192, "%s", url->valuestring);','out.url=url.valuestring;')
 .replace('snprintf(out->token, 256, "%s", token->valuestring);','out.token=token.valuestring;')
 .replace('out->url[n - 1] = 0','out.url=out.url.slice(0,-1)').replaceAll('->','.');
valid=valid.replace(/c == '([^']|\\\\)'/g,(_m,ch)=>'c == '+(ch==='\\\\'?92:ch.charCodeAt(0)));
vm.runInContext('function valid_profile(item,out){'+valid+'}',c);
let load=body('m3_relay_load').replaceAll('atomic_store(&profile_count, 0)','profile_count=0')
 .replaceAll('mbedtls_platform_zeroize(profiles, sizeof(profiles))','reset()').replace(/const cJSON \*/g,'const ')
 .replace(/\bint /g,'let ').replaceAll('&profiles[i]','profiles[i]').replaceAll('->','.')
 .replace('atomic_store(&selected, 0)','selected=0').replace('atomic_store(&profile_count, (unsigned)count)','profile_count=count');
vm.runInContext('function load(json){'+load+'}',c);
let select=body('m3_relay_select').replaceAll('unsigned ','let ').replace('m3_relay_count()','profile_count')
 .replace('atomic_load(&selected)','selected').replace('&profiles[index]','profiles[index]')
 .replace('*out = profiles[index]','Object.assign(out,profiles[index])').replace('atomic_store(&selected, index)','selected=index')
 .replace('memset(out, 0, sizeof(*out))','clear(out)');
vm.runInContext('function select(out){'+select+'}',c);
const p=(url,key)=>({url,token:'fake-'+key+'-token-for-host-tests-only'}),a=p('https://relay-a.example/','a'),b=p('https://relay-b.example','b');
assert(c.load({version:1,servers:[a,b]}));assert.equal(c.profiles[0].url,'https://relay-a.example');
c.online=new Set([b.token]);let out={};assert(c.select(out));assert.equal(out.url,b.url);assert.deepEqual(c.attempts,[a.token,b.token]);
const pinned={...out};c.attempts=[];c.online=new Set([a.token]);out={};assert(c.select(out));assert.equal(out.token,a.token);assert.equal(pinned.token,b.token,'job credentials stay pinned after another worker switches servers');
c.online.clear();out={old:true};assert(!c.select(out));assert.deepEqual(out,{});
for(const bad of [null,{version:2,servers:[a]},{version:1,servers:[]},{version:1,servers:Array(9).fill(a)},
 {version:1,servers:[{...a,url:'http://relay.example'}]},{version:1,servers:[{...a,url:'https://user:pass@relay.example'}]},
 {version:1,servers:[{...a,url:'https://relay.example/path'}]},{version:1,servers:[{...a,token:'short'}]},
 {version:1,servers:[{...a,url:'https://relay.example?secret=1'}]}]){
 assert(!c.load(bad));assert.equal(c.profile_count,0);assert(c.profiles.every(p=>!p.token),'invalid input leaves no partial credentials');}
const config=fs.readFileSync(path.join(__dirname,'../main/include/m3_tube_config.h'),'utf8');assert(!config.includes('m3_tube_private.h'));
console.log('PASS: actual C HTTPS/profile validation, bounded publication, auth/offline failover, preferred-server retry, immutable job copies, all-unavailable and malformed/absent SD settings. Host mocks only.');

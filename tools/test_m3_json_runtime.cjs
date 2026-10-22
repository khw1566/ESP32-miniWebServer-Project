// Run actual C selection/storage bodies against JSON and NVS/SD mocks.
// No firmware compiler, real keys, provider requests or physical SD access.
const fs=require('fs'),path=require('path'),vm=require('vm'),assert=require('assert');
const main=path.join(__dirname,'../mini-WEB (M3-ESP-IDF_ver)/main');
const read=name=>fs.readFileSync(path.join(main,name),'utf8');
function body(source,name){const m=new RegExp('\\b'+name+'\\([^;]*?\\)\\s*\\{').exec(source);assert(m,name);let start=m.index+m[0].length,i=start,depth=1;while(depth){if(source[i]==='{')depth++;if(source[i]==='}')depth--;i++;}return source.slice(start,i-1);}
const sd=read('m3_sd_settings.c');
const ctx={NULL:null,KEY_LIMIT:256,ESP_OK:0,ESP_FAIL:-1,ESP_ERR_INVALID_ARG:1,ESP_ERR_INVALID_STATE:2,ESP_ERR_TIMEOUT:3,
 NVS_NAMESPACE:'nvs',ready:true,lockable:true,locked:false,files:{},saved:null,cache:null,
 cJSON_GetObjectItemCaseSensitive:(o,k)=>o?.[k],cJSON_IsBool:v=>typeof v==='boolean',cJSON_IsTrue:v=>v===true,
 cJSON_IsString:v=>typeof v==='string',cJSON_IsNumber:v=>typeof v==='number',cJSON_IsObject:v=>!!v&&typeof v==='object'&&!Array.isArray(v),
 strlen:s=>Buffer.byteLength(s),memset:o=>{for(const k of Object.keys(o))delete o[k];},mbedtls_platform_zeroize:o=>{for(const k of Object.keys(o))delete o[k];},
 field:(o,name,s,key,n)=>{if(typeof o?.[name]!=='string'||Buffer.byteLength(o[name])>=n)return false;s[key]=o[name];return true;},
 network_settings_valid:s=>s.version===1&&!!s.ssid&&Buffer.byteLength(s.ssid)<=32&&(s.pass===''||Buffer.byteLength(s.pass)>=8)&&(!s.use_static||!!s.ip&&!!s.gateway&&!!s.netmask&&!!s.dns),
 sd_media_ready:()=>ctx.ready,sd_media_admin_lock:()=>{if(!ctx.lockable)return false;ctx.locked=true;return true;},sd_media_admin_unlock:()=>ctx.locked=false,
 read_keys:()=>ctx.files.keys??null,key_field:(j,name,out)=>{const value=j?.[name];if(typeof value!=='string'||value.length>=256||value.length&&value.length<16||/[^!-~]/.test(value))return false;out.value=value;return true;},
 cJSON_CreateObject:()=>({}),cJSON_DeleteItemFromObjectCaseSensitive:(o,k)=>delete o[k],cJSON_AddStringToObject:(o,k,v)=>{o[k]=v;return {value:v};},
 release_json:()=>{},mkdir:()=>0,PRIVATE_ROOT:'private',KEYS_PATH:'keys',errno:0,EEXIST:17,
 write_json:(p,j)=>{assert(ctx.locked);if(ctx.writeFails)return false;ctx.saved=structuredClone(j);ctx.files[p]=ctx.saved;return true;}
};
vm.createContext(ctx);
let profile=body(sd,'profile').replace('sizeof(*s)','0').replaceAll('s->','s.').replace('const cJSON *mode','const mode')
 .replace(/field\(obj, "(\w+)", s\.(\w+), sizeof\(s\.\2\)\)/g,(_m,n,k)=>`field(obj,"${n}",s,"${k}",${k==='ssid'?33:k==='pass'?65:16})`);
vm.runInContext('function profile(obj,s){'+profile+'}',ctx);
for(const data of [{ssid:'Fixture',password:'fixture-secret'},{version:1,ssid:'Fixture',password:'fixture-secret',static:false},{ssid:'Public',password:''}]){const out={};assert(ctx.profile(data,out));assert.equal(out.use_static,false);assert.equal(out.pass,data.password);}
assert(!ctx.profile({ssid:'Fixture',password:'short'},{}));assert(!ctx.profile({ssid:'Fixture',password:'fixture-secret',static:'false'},{}));
const staticData={ssid:'Fixture',password:'fixture-secret',static:true,ip:'192.168.1.20',gateway:'192.168.1.1',netmask:'255.255.255.0',dns:'1.1.1.1'};
assert(ctx.profile(staticData,{}));delete staticData.dns;assert(!ctx.profile(staticData,{}));
let copy=body(sd,'m3_sd_key_copy').replace('memset(out,0,KEY_LIMIT)','out.value=""').replace('bool absent; cJSON *json','let absent = {}; let json')
 .replace('&absent','absent').replace('bool ok','let ok').replace('out[0]','out.value.length');
vm.runInContext('function copy(name,out){'+copy+'}',ctx);
ctx.files.keys={gemini:'fake-shared-gemini-fixture',azure_speech:'fake-azure-fixture-key'};
let key={};assert(ctx.copy('gemini',key));assert.equal(key.value,'fake-shared-gemini-fixture');assert(!ctx.locked);
ctx.lockable=false;key={};assert(!ctx.copy('gemini',key));assert.equal(key.value,'');ctx.lockable=true;
let save=body(sd,'m3_sd_gemini_save').replace('!key ||','key == null ||').replace('bool absent; cJSON *json','let absent={}; let json').replaceAll('&absent','absent')
 .replace('if (!json && absent)','if (!json && absent.value)').replace('bool ok','let ok')
 .replace('char check[KEY_LIMIT]','let check = {}').replace('sizeof(check)','0');
vm.runInContext('function save(key){'+save+'}',ctx);
// read_keys returns an owned JSON tree, as cJSON does, so failed writes cannot mutate disk state.
ctx.read_keys=absent=>{absent.value=!ctx.files.keys;return ctx.files.keys?structuredClone(ctx.files.keys):null;};
assert.equal(ctx.save('fake-replacement-gemini-key'),0);assert.equal(ctx.saved.gemini,'fake-replacement-gemini-key');assert.equal(ctx.saved.azure_speech,'fake-azure-fixture-key');
assert.equal(ctx.save(''),0);assert.equal(ctx.saved.gemini,'');assert(!ctx.copy('gemini',{}));
ctx.writeFails=true;const previous=structuredClone(ctx.files.keys);assert.equal(ctx.save('fake-do-not-commit-key'),-1);assert.deepEqual(ctx.files.keys,previous);assert(!ctx.locked);ctx.writeFails=false;
ctx.ready=false;assert.equal(ctx.save('fake-unavailable-key'),2);ctx.ready=true;
// Select the M3 preprocessor branches without invoking a C preprocessor/compiler.
function preprocess(source,defined){let live=true,stack=[];return source.split('\n').filter(line=>{let m;if(m=line.match(/^\s*#ifdef (\w+)/)){stack.push({parent:live,condition:defined.has(m[1])});live=live&&defined.has(m[1]);return false;}if(/^\s*#else/.test(line)){const t=stack.at(-1);live=t.parent&&!t.condition;return false;}if(/^\s*#endif/.test(line)){live=stack.pop().parent;return false;}return live;}).join('\n');}
const network=read('network_settings.inc'),manager=read('network_manager.c');
assert(manager.includes('#define NETWORK_SETTINGS_LOAD_HOOK m3_sd_wifi_load'));assert(manager.includes('#define NETWORK_SETTINGS_SAVE_HOOK m3_sd_wifi_store'));
const hooks=new Set(['NETWORK_SETTINGS_LOAD_HOOK','NETWORK_SETTINGS_SAVE_HOOK']);
let load=preprocess(body(network,'network_settings_load'),hooks).replace('sizeof(*s)','0').replaceAll('s->','s.');
ctx.NETWORK_SETTINGS_LOAD_HOOK=s=>{if(!ctx.cache)return false;Object.assign(s,ctx.cache);return true;};
ctx.nvs_open=()=>{throw Error('M3 must not use NVS for credentials');};
vm.runInContext('function load(s){'+load+'}',ctx);
ctx.cache={version:1,ssid:'JSON Wi-Fi',pass:'fixture-password',use_static:0};const settings={};assert(ctx.load(settings));assert.equal(settings.ssid,'JSON Wi-Fi');
ctx.cache=null;assert(!ctx.load({}));
let store=preprocess(body(network,'network_save_configuration'),hooks)
 .replace('network_settings_t clean = { .version = 1, .use_static = s->use_static };','let clean = {version:1,use_static:s.use_static};')
 .replace(/snprintf\(clean\.(\w+), sizeof\(clean\.\1\), "%s", s->\1\)/g,'clean.$1=s.$1')
 .replace('esp_err_t err','let err').replaceAll('s->','s.').replaceAll('&clean','clean').replace('sizeof(clean)','0');
ctx.NETWORK_SETTINGS_SAVE_HOOK=s=>{ctx.cache=structuredClone(s);return ctx.saveResult;};ctx.saveResult=3;
vm.runInContext('function store(s){'+store+'}',ctx);assert.equal(ctx.store({version:1,ssid:'JSON Wi-Fi',pass:'fixture-secret'}),3);ctx.saveResult=0;assert.equal(ctx.store({version:1,ssid:'JSON Wi-Fi',pass:'fixture-secret'}),0);
// Both status and the real outbound provider path select the same JSON key.
const proxy=read('web_ai_proxy.inc'),security=read('web_ai_security.inc');
assert(proxy.includes('m3_sd_key_copy("gemini", api_key)'));assert(!proxy.includes('ai_key_decrypt'));assert(!proxy.includes('ai_record_read'));
assert(body(security,'ai_key_get_handler').includes('m3_sd_key_copy("gemini",key)'));
assert(body(security,'ai_key_post_handler').includes('m3_sd_gemini_save'));
assert(!body(security,'ai_key_post_handler').includes('ai_record_write'));
assert(!sd.includes('M3_GEMINI_API_KEY'));assert(!sd.includes('network_save_configuration'));
console.log('PASS: actual C flat Wi-Fi parsing, JSON-only load/save hooks and error propagation, shared web/device Gemini selection, partial provider files, key replacement/deletion preserving other providers, no NVS credential/key fallback (host mocks only).');

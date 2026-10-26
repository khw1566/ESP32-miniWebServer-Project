// Execute firmware selection/recovery bodies with JSON/filesystem mocks.
// No compiler, real credentials, SD access or hardware claims.
const fs = require('fs'), path = require('path'), vm = require('vm'), assert = require('assert');
const source = fs.readFileSync(path.join(__dirname, '../main/m4_sd_settings.c'), 'utf8');
function body(name) {
  const m = new RegExp('\\b' + name + '\\([^;]*?\\)\\s*\\{').exec(source);
  assert(m, name); let i = m.index + m[0].length, start = i, depth = 1;
  while (depth) { if (source[i] === '{') depth++; if (source[i] === '}') depth--; i++; }
  return source.slice(start, i - 1);
}
const ctx = {
  NULL:null, WIFI_PATH:'wifi', ready:true, locked:true, locks:0, files:{}, warnings:[],
  sd_media_ready:()=>ctx.ready,
  sd_media_admin_lock:()=>{if(!ctx.locked)return false;ctx.locks++;return true;},
  sd_media_admin_unlock:()=>ctx.locks--,
  read_json:(p,absent)=>{absent.value=!(p in ctx.files);return ctx.files[p]??null;},
  history_valid:j=>!!j && (j.version===undefined||j.version===1) && (Array.isArray(j.networks)?j.networks:[j]).every(p=>typeof p.ssid==='string' && !!p.ssid && typeof p.password==='string'),
  release_json:()=>{},ESP_LOGW:(_tag,message)=>ctx.warnings.push(message),
  cJSON_GetObjectItemCaseSensitive:(j,k)=>j?.[k],cJSON_IsString:v=>typeof v==='string',
  strcmp:(a,b)=>a===b?0:1,profile:(p,out)=>{Object.assign(out,p);return true;}
};
vm.createContext(ctx);
let recovery=body('read_history').replace('cJSON *json','let json')
  .replace('bool backup_absent;','let backup_absent = {};')
  .replaceAll('&backup_absent','backup_absent').replaceAll('*absent','absent.value')
  .replace('&& backup_absent','&& backup_absent.value').replace('WIFI_PATH ".bak"','WIFI_PATH + ".bak"');
vm.runInContext('function read_history(absent){'+recovery+'}',ctx);
let find=body('m4_sd_wifi_find').replace('!ssid ||','ssid == null ||').replace('!*ssid','!ssid.length').replaceAll('*ssid','ssid.length')
  .replace('bool absent, found = false;','let absent = {}, found = false;')
  .replace('cJSON *json','let json').replace('const cJSON *last','const last')
  .replace('const char *wanted','const wanted').replaceAll('last->valuestring','last')
  .replace('const cJSON *item, *fallback = NULL;','let item, fallback = null;')
  .replace('cJSON_ArrayForEach(item, cJSON_GetObjectItemCaseSensitive(json, "networks"))','for (item of cJSON_GetObjectItemCaseSensitive(json, "networks"))')
  .replaceAll('const cJSON *name','const name').replaceAll('name->valuestring','name')
  .replaceAll('&absent','absent');
vm.runInContext('function find(ssid,out){'+find+'}',ctx);
const a={ssid:'Fixture-A',password:'fixture-a'}, b={ssid:'Fixture-B',password:'fixture-b'};
const history=(last)=>({version:1,networks:[a,b],...(last===undefined?{}:{last_ssid:last})});
function lookup(files,ssid='') {
 ctx.files=files;ctx.warnings=[];const out={};const found=ctx.find(ssid,out);
 assert.equal(ctx.locks,0);return {found,out};
}
assert.equal(lookup({wifi:a}).out.ssid,'Fixture-A');
assert(!lookup({wifi:a},'Fixture-B').found);
assert.equal(lookup({wifi:history('Fixture-A')}).out.ssid,'Fixture-A');
assert.equal(lookup({wifi:history()}).out.ssid,'Fixture-B');
assert.equal(lookup({wifi:history('Deleted')}).out.ssid,'Fixture-B');
assert(!lookup({wifi:history()},'Not-Saved').found);
assert.equal(lookup({wifi:history()},'Fixture-A').out.password,'fixture-a');
assert.equal(lookup({wifi:{broken:true},'wifi.bak':history('Fixture-A')}).out.ssid,'Fixture-A');
assert(ctx.warnings.includes('Using Wi-Fi recovery backup'));
assert.equal(lookup({'wifi.bak':history('Fixture-B')}).out.ssid,'Fixture-B');
assert(!lookup({wifi:{broken:true},'wifi.bak':{broken:true}}).found);
assert(!lookup({}).found);
ctx.ready=false;assert(!lookup({wifi:history()}).found);ctx.ready=true;
ctx.locked=false;assert(!lookup({wifi:history()}).found);ctx.locked=true;
console.log('SD Wi-Fi recovery: last selection, fallback, exact lookup, corrupt/missing primary and backup, unavailable/busy SD passed (host mocks only).');

// Execute actual C history and scan-selection bodies with fake JSON/radio data.
// No compiler, real credentials, SD I/O or hardware assertions.
const fs=require('fs'),path=require('path'),vm=require('vm'),assert=require('assert');
const root=path.resolve(__dirname,'..');
const read=p=>fs.readFileSync(path.join(root,p),'utf8').replace(/\r\n/g,'\n');
const sd=read('main/m3_sd_settings.c'),net=read('main/network_manager.c'),runtime=read('main/m3_runtime.c');
function body(source,name){const m=new RegExp('\\b'+name+'\\([^;]*?\\)\\s*\\{').exec(source);assert(m,name);
 let i=m.index+m[0].length,start=i,depth=1;while(depth){if(source[i]==='{')depth++;if(source[i]==='}')depth--;i++;}return source.slice(start,i-1);}
const each=n=>{const a=[];for(let p=n?.child;p;p=p.next)a.push(p);return a;};
function node(value){if(value===null||value===undefined)return null;
 const n={child:null,next:null};
 if(Array.isArray(value)){n.array=true;for(const v of value)append(n,node(v));}
 else if(typeof value==='object'){n.members={};for(const [k,v] of Object.entries(value))n.members[k]=node(v);}
 else {n.scalar=value;if(typeof value==='string')n.valuestring=value;}
 return n;}
function append(a,n){if(a.child)each(a).at(-1).next=n;else a.child=n;return true;}
function plain(n){if(n.array)return each(n).map(plain);if(n.members)return Object.fromEntries(Object.entries(n.members).map(([k,v])=>[k,plain(v)]));return n.scalar;}
const ctx={NULL:null,M3_WIFI_PROFILE_LIMIT:32,ESP_OK:0,ESP_ERR_NO_MEM:257,ESP_FAIL:-1,
 cJSON_CreateObject:()=>node({}),cJSON_CreateArray:()=>node([]),
 cJSON_AddNumberToObject:(n,k,v)=>(n.members[k]=node(v)),
 cJSON_AddStringToObject:(n,k,v)=>(n.members[k]=node(v)),
 cJSON_AddItemToObject:(n,k,v)=>(n.members[k]=v,true),
 cJSON_GetObjectItemCaseSensitive:(n,k)=>n?.members?.[k]??null,
 cJSON_IsString:n=>typeof n?.scalar==='string',cJSON_Duplicate:n=>node(plain(n)),
 cJSON_AddItemToArray:append,release_json:()=>{},each,
 strcmp:(a,b)=>a===b?0:1,
 profile_json:s=>node({ssid:s.ssid,password:s.pass,static:!!s.use_static,...s.use_static?{ip:s.ip,gateway:s.gateway,netmask:s.netmask,dns:s.dns}:{}})};
vm.createContext(ctx);
let update=body(sd,'updated_history')
 .replace(/\b(?:const )?cJSON\s*\*/g,'let ').replace(/\b(?:const )?char\s*\*/g,'let ')
 .replace(/\b(?:bool|unsigned)\s+/g,'let ').replaceAll(', *networks',', networks').replaceAll('->','.').replaceAll('*error','error.value')
 .replace(/cJSON_ArrayForEach\((\w+), (\w+)\)/g,'for ($1 of each($2))');
vm.runInContext('function update(old,saved,removed,error){'+update+'}',ctx);
const fixture=(i)=>({ssid:'Fixture-'+i,password:'fixture-password-'+i,static:false});
function change(old,saved=null,removed=null){const before=structuredClone(old),error={};
 const result=ctx.update(node(old),saved,removed,error);assert.deepEqual(old,before,'building a replacement never changes committed data');
 return {json:result?plain(result):null,error:error.value};}
let old={ssid:'Legacy',password:'legacy-fixture',static:true,ip:'192.168.1.40',gateway:'192.168.1.1',netmask:'255.255.255.0',dns:'1.1.1.1'};
let changed=change(old,{ssid:'Second',pass:'second-fixture'});
assert.equal(changed.error,0);assert.deepEqual(changed.json.networks[0],old);assert.equal(changed.json.last_ssid,'Second');
old=changed.json;
for(let i=2;i<32;i++){changed=change(old,{ssid:'Fixture-'+i,pass:'fixture-password-'+i});assert.equal(changed.error,0);old=changed.json;}
assert.equal(old.networks.length,32);
assert.equal(change(old,{ssid:'Overflow',pass:'overflow-fixture'}).error,257,'33rd SSID is rejected without eviction');
changed=change(old,{ssid:'Legacy',pass:'replacement-fixture',use_static:1,ip:'10.0.0.20',gateway:'10.0.0.1',netmask:'255.255.255.0',dns:'1.1.1.1'});
assert.equal(changed.json.networks.length,32);assert.equal(changed.json.networks.at(-1).ip,'10.0.0.20');
assert.deepEqual(changed.json.networks.slice(0,-1),old.networks.slice(1),'password/IP update preserves all other profiles');
changed=change(changed.json,null,'Fixture-7');assert.equal(changed.json.networks.length,31);
assert(!changed.json.networks.some(p=>p.ssid==='Fixture-7'));
assert.equal(change(changed.json,{ssid:'New slot',pass:'new-slot-fixture'}).json.networks.length,32);
changed=change(null,{ssid:'First',pass:'first-fixture'});assert.equal(changed.json.networks.length,1);
assert.equal(change(changed.json,null,'First').json.networks.length,0);
const duplicated={version:1,networks:[fixture(0),fixture(0),fixture(1)]};
assert.equal(change(duplicated,{ssid:'Third',pass:'third-fixture'}).json.networks.length,3);

// Actual store flow: do not publish a replacement until the SD commit succeeds.
Object.assign(ctx,{ESP_ERR_INVALID_ARG:258,ESP_ERR_INVALID_STATE:259,ESP_ERR_TIMEOUT:260,
 WIFI_PATH:'wifi',PRIVATE_ROOT:'private',EEXIST:17,errno:0,ready:true,available:true,writeFails:false,
 network_settings_valid:s=>!!s.ssid,sd_media_ready:()=>ctx.ready,sd_media_admin_lock:()=>ctx.available,
 sd_media_admin_unlock:()=>{},mkdir:()=>0,ESP_LOGI:()=>{},ESP_LOGW:()=>{},
 read_history:absent=>{absent.value=ctx.absent;return ctx.committed?node(ctx.committed):null;},
 updated_history:(...args)=>ctx.update(...args),
 write_json:(_p,n)=>{if(ctx.writeFails)return false;ctx.committed=plain(n);return true;},
 cache_history:n=>{ctx.published=plain(n);},m3_sd_wifi_activate:ssid=>{ctx.active=ssid;return true;}});
let store=body(sd,'m3_sd_wifi_store')
 .replace('bool absent; cJSON *previous = read_history(&absent);','let absent={}; let previous=read_history(absent);')
 .replace('esp_err_t error = ESP_FAIL;','let error={value:ESP_FAIL};')
 .replace('cJSON *json','let json').replace(/\bbool\s+/g,'let ')
 .replaceAll('&error','error').replace('previous || absent','previous || absent.value').replaceAll('s->','s.')
 .replace('error != ESP_OK ? error : ESP_FAIL','error.value != ESP_OK ? error.value : ESP_FAIL');
vm.runInContext('function store(s){'+store+'}',ctx);
ctx.committed={version:1,networks:[fixture(0)]};ctx.published=structuredClone(ctx.committed);ctx.absent=false;
assert.equal(ctx.store({ssid:'Stored',pass:'stored-fixture'}),0);assert.equal(ctx.published.networks.length,2);
const committed=structuredClone(ctx.committed),published=structuredClone(ctx.published);
ctx.writeFails=true;assert.equal(ctx.store({ssid:'Do not publish',pass:'failed-fixture'}),-1);
assert.deepEqual(ctx.committed,committed);assert.deepEqual(ctx.published,published);ctx.writeFails=false;
ctx.committed=null;ctx.absent=false;assert.equal(ctx.store({ssid:'Unreadable history',pass:'failed-fixture'}),-1);
assert.deepEqual(ctx.published,published,'unreadable history is retained, never replaced with an empty history');
ctx.absent=true;assert.equal(ctx.store({ssid:'First save',pass:'first-fixture'}),0);
ctx.ready=false;assert.equal(ctx.store({ssid:'No SD',pass:'fixture'}),259);ctx.ready=true;
ctx.available=false;assert.equal(ctx.store({ssid:'Busy SD',pass:'fixture'}),260);ctx.available=true;

// Actual scan result loop: skip strangers before truncating saved candidates.
let records=body(net,'scan_wifi');records=records.slice(records.indexOf('int valid_count = 0;'),records.indexOf('    free(ap_records);',records.indexOf('int valid_count = 0;')));
records=records.replace(/\b(?:int|bool)\s+/g,'let ').replaceAll('(char *)','')
 .replace(/strncpy\(items\[valid_count\]\.ssid,[\s\S]*?sizeof\(items\[valid_count\]\.ssid\) - 1\);/,'items[valid_count] = {ssid:ap_records[i].ssid};')
 .replace(/items\[valid_count\]\.ssid\[sizeof\(items\[valid_count\]\.ssid\) - 1\] = '\\0';/,'');
ctx.WIFI_AUTH_OPEN=0;ctx.strlen=s=>s.length;ctx.known=new Set(['Weak saved','Strong saved']);ctx.m3_sd_wifi_matches=(s,open)=>ctx.known.has(s)&&!open;
vm.runInContext('function records(ap_records,max_items,saved_only,target=null){const items=[];const fetch_count=ap_records.length;'+records+'return items;}',ctx);
const visible=[{ssid:'Strong saved',rssi:-40,authmode:1},...Array.from({length:200},(_,i)=>({ssid:'Stranger-'+i,rssi:-50,authmode:1})),
 {ssid:'Weak saved',rssi:-80,authmode:1},{ssid:'Weak saved',rssi:-90,authmode:1},{ssid:'',rssi:-100,authmode:1}];
assert.deepEqual(Array.from(ctx.records(visible,32,true),p=>p.ssid),['Strong saved','Weak saved']);
assert.equal(ctx.records(visible,8,false).length,8);
assert.equal(ctx.records(visible,1,false,'Weak saved')[0].ssid,'Weak saved','explicit selection can find a weak AP beyond 200 strangers');
assert.equal(ctx.records([{ssid:'Weak saved',rssi:-30,authmode:0},...visible],32,true).at(-1).authmode,1,'open AP with a matching name cannot mask the saved secured AP');

// Actual auto-connect loop, preserving each candidate's address and secret.
const radio={NULL:null,ESP_OK:0,ESP_ERR_NOT_FOUND:261,WIFI_AUTH_OPEN:0,M3_WIFI_PROFILE_LIMIT:32,
 m3_sd_wifi_profiles:()=>radio.profiles.length,atomic_store:()=>{},
 scan_wifi:(items,limit,saved)=>{assert(saved);radio.events.push('scan');radio.candidates.forEach(p=>items.push({...p}));return radio.scanResult??items.length;},
 m3_sd_wifi_activate:ssid=>{radio.selected=radio.profiles.find(p=>p.ssid===ssid);return !!radio.selected;},
 m3_sd_wifi_load:s=>{Object.assign(s,radio.selected);return true;},
 connect_sta_visible:(ssid,pass)=>{assert.equal(pass,radio.selected.pass);radio.events.push(ssid);return radio.results[ssid]??-1;},
 clear:s=>{for(const k of Object.keys(s))s[k]='';}};
vm.createContext(radio);
let connect=body(net,'network_connect_saved').replaceAll('(esp_err_t)','').replaceAll('&s_rescan_pending','null')
 .replace('wifi_scan_item_t candidates[M3_WIFI_PROFILE_LIMIT];','const candidates=[];')
 .replace('network_settings_t settings;','let settings={};')
 .replace(/\b(?:int|bool|esp_err_t)\s+/g,'let ').replaceAll('&settings','settings')
 .replace(/memset\(settings, 0, sizeof\(settings\)\)/g,'clear(settings)')
 .replace('settings.pass[0] == 0','!settings.pass.length');
vm.runInContext('function connect(){'+connect+'}',radio);
function attempt(overrides={}){Object.assign(radio,{events:[],profiles:[{ssid:'Strong saved',pass:'strong-fixture',ip:'10.0.0.2'},
 {ssid:'Weak saved',pass:'weak-fixture',ip:'192.168.1.2'}],candidates:visible.filter(p=>ctx.known.has(p.ssid)).slice(0,2),results:{'Weak saved':0},scanResult:null},overrides);return radio.connect();}
assert.equal(attempt(),0);assert.deepEqual(radio.events,['scan','Strong saved','Weak saved']);
assert.equal(radio.selected.ip,'192.168.1.2');
assert.equal(attempt({results:{'Strong saved':0}}),0);assert.deepEqual(radio.events,['scan','Strong saved']);
assert.equal(attempt({candidates:[]}),261);assert.deepEqual(radio.events,['scan']);
assert.equal(attempt({scanResult:-259}),259);assert.deepEqual(radio.events,['scan']);
assert.equal(attempt({profiles:[]}),261);assert.deepEqual(radio.events,[]);
assert.equal(attempt({candidates:[{ssid:'Strong saved',authmode:0}]}),261,'open/secured mismatch is skipped');
assert(!body(net,'recovery_event').includes('esp_wifi_connect('),'event loop never reconnects blindly');
assert(!body(net,'wifi_event_handler').slice(body(net,'wifi_event_handler').indexOf('WIFI_EVENT_STA_DISCONNECTED')).includes('esp_wifi_connect('));
assert(body(runtime,'services').includes('network_connect_saved()'));
assert(body(sd,'m3_sd_wifi_store').indexOf('cache_history(json)')>body(sd,'m3_sd_wifi_store').indexOf('write_json(WIFI_PATH,json)'));
console.log('PASS: 32 slots, legacy migration, update/delete/full preservation, per-SSID IP/passwords, duplicate/hidden/200-stranger scans, strongest-first failover, absent/error scans and scan-based boot/recovery (actual C bodies with host mocks).');

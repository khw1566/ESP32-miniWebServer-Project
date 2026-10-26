// Execute the C URL/selector/update policies with host mocks. No native build or live network.
const fs=require('fs'),path=require('path'),vm=require('vm'),net=require('net'),assert=require('assert');
const root=path.resolve(__dirname,'..'),read=p=>fs.readFileSync(path.join(root,p),'utf8');
const ui=read('main/m4_ui.c'),runtime=read('main/m4_runtime.c'),relay=read('main/cloud_relay.c');
function body(source,name){
 const m=new RegExp('\\b'+name+'\\([^)]*\\)\\s*\\{').exec(source);assert(m,name);
 let start=m.index+m[0].length,end=start,depth=1;
 while(depth){if(source[end]==='{')depth++;if(source[end]==='}')depth--;end++;}
 return source.slice(start,end-1);
}
const c={AF_INET6:6,ESP_OK:0,ESP_ERR_INVALID_ARG:1,ESP_ERR_INVALID_STATE:2,ESP_ERR_INVALID_SIZE:3,
 M4_WEB_DOMAIN:0,M4_WEB_IPV6:1,M4_WEB_IPV4:2,M4_WEB_MDNS:3,NET_MODE_STA_CONNECTED:3,
 inet_pton:(af,src,dst)=>{
  if(af!==6||net.isIP(src)!==6)return 0;
  const head=src.split(':')[0];dst.s6_addr[0]=head?parseInt(head,16)>>8:0;return 1;
 },
 snprintf:(out,cap,format,value)=>{const text=format.replace('%s',value);out.value=text.slice(0,Math.max(0,cap-1));return Buffer.byteLength(text);},
 cloud_relay_is_online:()=>c.online,online:false,public_url:'https://m4-example.audreaming.com/'
};vm.createContext(c);
let code=body(runtime,'ipv6_url').replace('struct in6_addr parsed;','const parsed={s6_addr:new Uint8Array(16)};').replace('&parsed','parsed')
 .replace('int n =','let n =').replaceAll('(size_t)n','n').replaceAll('out[0] = 0','out.value = ""');
vm.runInContext('function ipv6_url(address,out,capacity){'+code+'}',c);
for(const address of ['2001:db8::1','2406:da14:1:2::1234','3fff:ffff:ffff:ffff:ffff:ffff:ffff:ffff','2000::']){
 const out={value:'stale'};assert(c.ipv6_url(address,out,96));assert.equal(out.value,`http://[${address}]/`);
 const parsed=new URL(out.value);assert.equal(parsed.protocol,'http:');assert.equal(parsed.pathname,'/');
 const exact=Buffer.byteLength(out.value)+1;assert(c.ipv6_url(address,out,exact));
 assert(!c.ipv6_url(address,out,exact-1));assert.equal(out.value,'');
}
for(const address of [null,'','미발급','fe80::1','fe80::1%4','fc00::1','fd12::1','::','::1','ff02::1','192.168.4.1','2001:db8:::1','2001:evil::1','2001:db8::1/path','2001::1]@example.com']){
 const out={value:'stale'};assert(!c.ipv6_url(address,out,96),String(address));assert.equal(out.value,'');
}
assert(!c.ipv6_url('2001:db8::1',null,96));assert(!c.ipv6_url('2001:db8::1',{},0));
code=body(read('main/include/m4_web_address.h'),'m4_web_address_url').replaceAll('->','.');
vm.runInContext('function m4_web_address_url(s,kind){'+code+'}',c);
const snapshot={web:true,mode:3,mdns_ready:true,cloud:true,domain_url:'https://m4-example.audreaming.com/',
 ipv6_url:'http://[2001:db8::1]/',url:'http://192.168.0.42/',mdns_url:'http://m4-example.local/',requests:42,
 cloud_status:'online',ipv6_status:'global_ready',wifi_busy:false};
for(const [kind,field] of [[0,'domain_url'],[1,'ipv6_url'],[2,'url'],[3,'mdns_url']])assert.equal(c.m4_web_address_url(snapshot,kind),snapshot[field]);
assert.equal(c.m4_web_address_url(snapshot,99),'');
assert.equal(c.m4_web_address_url({...snapshot,mode:1},1),'');
assert.equal(c.m4_web_address_url({...snapshot,mdns_ready:false},3),'');
for(const kind of [1,2,3])assert.equal(c.m4_web_address_url({...snapshot,web:false},kind),'');
assert.equal(c.m4_web_address_url({...snapshot,cloud:false},0),snapshot.domain_url,'domain remains visible offline');
for(const name of ['cloud_relay_get_public_url','cloud_relay_get_url']){
 code=body(relay,name).replace('int n =','let n =').replaceAll('(size_t)n','n')
  .replaceAll('out[0] = 0','out.value = ""');
 vm.runInContext(`function ${name}(out,length){${code}}`,c);
}
let out={value:'stale'};assert.equal(c.cloud_relay_get_url(out,96),2);assert.equal(out.value,'');
assert.equal(c.cloud_relay_get_public_url(out,96),0);assert.equal(out.value,c.public_url);
assert.equal(c.cloud_relay_get_public_url(out,8),3);assert.equal(out.value,'','never expose a truncated QR URL');
c.online=true;assert.equal(c.cloud_relay_get_url(out,96),0);assert.equal(out.value,c.public_url);
c.public_url='';assert.equal(c.cloud_relay_get_public_url(out,96),2);assert.equal(out.value,'');
assert.equal(c.cloud_relay_get_public_url(null,96),1);

// Run the actual UI cache and visibility logic through address changes and failures.
const ids=[...read('main/include/m4_i18n.h').matchAll(/\b(?:TITLE|TEXT)\((\w+)\)/g)].map(m=>'T_'+m[1]);
for(const id of ids)c[id]=id;
Object.assign(c,{snapshot,web_address_kind:0,web_qr_dirty:true,web_qr_ok:false,seen_web_url:'',web_tabs:{},
 web_qr:{},web_frame:{},web_hint:{},web_retry:{},web_url:{},web_state:{},web_dot:{},web_requests:{},
 LV_OBJ_FLAG_HIDDEN:1,LV_STATE_DISABLED:2,LV_RESULT_OK:0,GREEN:1,AMBER:2,qr_calls:[],qr_fail:false,
 tr:id=>id,strcmp:(a,b)=>a===b?0:1,strlen:s=>s.length,
 set_text:(obj,value)=>obj.text=value,set_bg:(obj,value)=>obj.bg=value,
 lv_qrcode_update:(obj,url,len)=>{assert.equal(len,Buffer.byteLength(url));c.qr_calls.push(url);return c.qr_fail?1:0;},
 lv_obj_add_flag:obj=>obj.hidden=true,lv_obj_remove_flag:obj=>obj.hidden=false,
 lv_obj_add_state:obj=>obj.disabled=true,lv_obj_remove_state:obj=>obj.disabled=false,
});
code=body(ui,'update_web').replace('char text[96];','let text={};').replace('const char *url','const url').replace('&snapshot','snapshot')
 .replace('bool ready','let ready').replace('snprintf(seen_web_url, sizeof(seen_web_url), "%s", url)','seen_web_url = url')
 .replace('snprintf(text, sizeof(text), tr(T_JOB_REQUESTS), snapshot.requests)','text.value = String(snapshot.requests)')
 .replace('set_text(web_requests, text)','set_text(web_requests, text.value)');
vm.runInContext('function web_explanation(){'+body(ui,'web_explanation')+'}\nfunction update_web(){'+code+'}',c);
c.update_web();assert.equal(c.qr_calls.at(-1),snapshot.domain_url);assert(!c.web_frame.hidden);
c.update_web();assert.equal(c.qr_calls.length,1,'unchanged QR does not reallocate every tick');
function select(kind){c.web_address_kind=kind;c.web_qr_dirty=true;c.update_web();}
select(1);assert.equal(c.qr_calls.at(-1),snapshot.ipv6_url);
snapshot.ipv6_url='http://[2001:db8::2]/';c.update_web();assert.equal(c.qr_calls.at(-1),snapshot.ipv6_url);
snapshot.ipv6_url='';snapshot.ipv6_status='no_global_prefix';c.update_web();assert(c.web_frame.hidden);
assert.equal(c.web_hint.text,'T_WEB_NO_PREFIX');assert.equal(c.web_url.text,'T_QR_WAIT');
snapshot.mode=1;c.update_web();assert(c.web_retry.disabled);assert.equal(c.web_hint.text,'T_WEB_NO_WIFI');
snapshot.mode=3;select(2);assert.equal(c.qr_calls.at(-1),snapshot.url);
select(3);assert.equal(c.qr_calls.at(-1),snapshot.mdns_url);
snapshot.mdns_ready=false;c.update_web();assert(c.web_frame.hidden);
snapshot.cloud=false;snapshot.cloud_status='factory_missing';select(0);
assert(!c.web_frame.hidden);assert.equal(c.web_hint.text,'T_WEB_PROVISION');assert.equal(c.web_dot.bg,c.AMBER);
for(const [status,id] of [['waiting_wifi','T_WEB_NO_WIFI'],['waiting_time','T_WEB_CLOCK'],['waiting_memory','T_WEB_MEMORY'],['paused_ai','T_WEB_PAUSED'],['connecting','T_WEB_DOMAIN_WAIT']]){
 snapshot.cloud_status=status;c.update_web();assert.equal(c.web_hint.text,id);
}
c.qr_fail=true;select(0);assert(c.web_frame.hidden);assert.equal(c.web_hint.text,'T_WEB_QR_ERROR');
const count=c.qr_calls.length;c.update_web();assert.equal(c.qr_calls.length,count,'no allocation retry loop');
c.qr_fail=false;select(0);assert(!c.web_frame.hidden);
snapshot.domain_url='';c.update_web();assert(c.web_frame.hidden);assert.equal(c.web_url.text,'T_QR_WAIT');
c.web_qr_dirty=true;c.update_web();assert.equal(c.web_url.text,'T_QR_WAIT','empty cache still reapplies translated pending text');

const apps=/static const app_id_t apps\[\] = \{([^}]+)\}/.exec(ui)[1].split(',').map(s=>s.trim());
assert.equal(apps[0],'APP_MONITOR','server is on the first home tile');
assert(ui.includes('{T_WEB_DOMAIN, T_WEB_IPV6, T_WEB_IPV4, T_WEB_MDNS}'));
const build=body(ui,'build_monitor');assert(!build.includes('lv_obj_add_flag(web_tabs, LV_OBJ_FLAG_HIDDEN)'));
assert(build.includes('lv_qrcode_set_size(web_qr, 216)'));assert(build.includes('lv_obj_set_size(web_frame, 280, 280)'));
assert(runtime.includes('next.ipv6_url[0] = 0;'),'clear before every current-address query');
assert(runtime.includes('next.mode != NET_MODE_AP_CAPTIVE && next.mode != NET_MODE_STA_CONNECTED'));
assert(runtime.includes('cloud_relay_get_url(next.cloud_url, sizeof(next.cloud_url))'),'keep legacy online-only API contract');
console.log('PASS: explicit domain/IPv6/IPv4/mDNS selection, global IPv6 brackets and rejected addresses, bounded URL APIs, offline provisioning, QR cache/invalidation/failure, first-home access and quiet zone. Network/LVGL calls are mocked.');

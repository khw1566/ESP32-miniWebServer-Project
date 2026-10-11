const fs=require('fs'),path=require('path'),vm=require('vm'),assert=require('assert');
const root=path.resolve(__dirname,'..'),common=path.join(__dirname,'firmware_common');
const read=f=>fs.readFileSync(f,'utf8');
const html=read(path.join(common,'wifi_setup.html'));
const script=html.match(/<script>([\s\S]*?)<\/script>/)[1];
class Element {
  constructor(id){this.id=id;this.value=id==='ip_mode'?'dhcp':id==='netmask'?'255.255.255.0':'';this.hidden=false;this.disabled=false;this.children=[];this.listeners={};this.textContent='';}
  addEventListener(type,fn){this.listeners[type]=fn;}
  appendChild(child){this.children.push(child);}
}
function setup(response={ok:true,data:{status:'ok',server_url:'http://192.168.0.50/',product_url:'http://p4-v1-2-test.local/'}}){
  const elements={},el=id=>elements[id]||(elements[id]=new Element(id)),calls=[];
  const fields=['ssid','pass','ip_mode','ip','gateway','netmask','dns','admin_pw'];
  const context={document:{getElementById:el,createElement:()=>new Element('option')},TextEncoder,URLSearchParams,
    FormData:class {constructor(){return fields.map(id=>[id,el(id).value]);}},
    fetch:async(url,options)=>{calls.push({url,options});if(url==='/api/network')return {ok:false,json:async()=>({})};return {ok:response.ok,json:async()=>response.data};},console};
  vm.createContext(context);vm.runInContext(script,context);calls.length=0;
  return {context,el,calls};
}
const good={ip_mode:'static',ip:'192.168.0.50',gateway:'192.168.0.1',netmask:'255.255.255.0',dns:'192.168.0.1'};
async function run(){
  const {context:c,el,calls}=setup();
  assert(c.validAddress(good));
  assert(c.validAddress({...good,dns:''}));
  assert(c.validAddress({ip_mode:'dhcp'}));
  const invalid=[{ip:'999.168.0.50'},{ip:'192.168.0.0'},{ip:'192.168.0.255'},
    {ip:'192.168.0.1'},{gateway:'192.168.1.1'},{gateway:'192.168.0.255'},
    {netmask:'255.0.255.0'},{netmask:'0.0.0.0'},{netmask:'255.255.255.255'},
    {netmask:'255.255.255.254'},{dns:'invalid'},{dns:'127.0.0.1'},
    {ip:'192.168.4.10',gateway:'192.168.4.1'},{netmask:'255.255.0.0'},
    {ip:'224.0.0.1'},{ip:'127.0.0.1'},{ip:'0.1.2.3'},{ip:'192.168.0.1 '},
    {ip:'192.168.0.1/24'},{dns:'255.255.255.255'}];
  for(const patch of invalid)assert(!c.validAddress({...good,...patch}),JSON.stringify(patch));
  el('ip_mode').value='static';c.modeChanged();assert.equal(el('staticFields').hidden,false);assert(el('ip').required);
  el('ip_mode').value='dhcp';c.modeChanged();assert.equal(el('staticFields').hidden,true);assert.equal(el('ip').required,false);
  el('ssid').value='한'.repeat(11);el('pass').value='password1';await c.saveSettings({preventDefault(){}});assert.equal(calls.length,0);assert(el('status').textContent.includes('32'));
  el('ssid').value='Test & WiFi';el('pass').value='short';await c.saveSettings({preventDefault(){}});assert.equal(calls.length,0);
  for(const [key,value] of Object.entries(good))el(key).value=value;
  el('pass').value='p&ss+word1';el('admin_pw').value='admin-secret';await c.saveSettings({preventDefault(){}});
  assert.equal(calls.length,1);assert.equal(calls[0].url,'/api/wifi');
  const sent=new URLSearchParams(calls[0].options.body);assert.equal(sent.get('ssid'),'Test & WiFi');assert.equal(sent.get('pass'),'p&ss+word1');assert.equal(sent.get('ip'),'192.168.0.50');assert.equal(sent.get('dns'),'192.168.0.1');
  assert.equal(el('serverLink').href,'http://192.168.0.50/');assert(el('setupForm').hidden);assert.equal(el('pass').value,'');assert.equal(el('admin_pw').value,'');
  const failure=setup({ok:false,data:{message:'NVS write failed'}});failure.el('ssid').value='Test';failure.el('pass').value='password1';await failure.context.saveSettings({preventDefault(){}});assert.equal(failure.el('pass').value,'password1');assert.equal(failure.el('save').disabled,false);assert.equal(failure.el('scan').disabled,false);assert.equal(failure.el('setupForm').hidden,false);assert(failure.el('status').textContent.includes('NVS'));
  const scan=setup({ok:true,data:{networks:[{ssid:'<script>alert(1)</script>',rssi:-45}]}});await scan.context.scanWifi();assert.equal(scan.el('networks').children[0].value,'<script>alert(1)</script>');assert.equal(scan.el('scan').disabled,false);
  for(const project of fs.readdirSync(root).filter(n=>n.startsWith('mini-WEB'))){
    const main=path.join(root,project,'main');
    for(const file of ['network_settings.inc','web_network_setup.inc'])assert.equal(read(path.join(main,file)),read(path.join(common,file)),`${project}/${file} stale`);
    assert.equal(read(path.join(main,'include/network_settings.h')),read(path.join(common,'network_settings.h')));
    const embedded=read(path.join(main,'web_wifi_setup_html.inc'));
    const decoded=[...embedded.matchAll(/^"(?:\\.|[^"\\])*"/gm)].map(m=>JSON.parse(m[0])).join('');assert.equal(decoded,html);
    const net=read(path.join(main,'network_manager.c')),web=read(path.join(main,'web_server.c'));
    assert(net.includes('#include "network_settings.inc"'));assert(net.indexOf('err = network_apply_sta_address();')<net.indexOf('// 플래그 먼저 세팅 후 start'));
    assert(net.includes('WIFI_STORAGE_RAM'));assert(web.includes('.uri="/api/network"'));assert(web.includes('.uri="/api/wifi/scan"'));
    assert(!web.includes('501 Not Implemented'));assert(web.includes('HTML_WIFI_SETUP'));
  }
  const backend=read(path.join(common,'web_network_setup.inc')),settings=read(path.join(common,'network_settings.inc'));
  assert(backend.includes('verify_admin_password(admin)'));assert(backend.includes('network_setup_client_local(httpd_req_to_sockfd(req))'));
  const get=backend.slice(backend.indexOf('static esp_err_t api_network_get_handler'),backend.indexOf('static esp_err_t api_wifi_scan_handler'));
  assert(!get.includes('"pass"'));assert(!get.includes('"admin_pw"'));
  assert(settings.includes('nvs_set_blob(h, "setup_cfg"'));assert(settings.includes('if (err == ESP_OK) err = nvs_commit(h)'));
  assert(settings.includes('esp_netif_set_dns_info'));assert(settings.includes('esp_netif_dhcpc_start'));
  assert(settings.includes('network_apply_setup_ap_address'));assert(settings.includes('esp_netif_dhcps_start(s_ap_netif)'));
  assert(settings.includes('getpeername(socket_fd'));assert(settings.includes('ESP_ERR_NVS_NOT_FOUND'));
  const m1=read(path.join(root,'mini-WEB (M1-ESP-IDF_ver)/main/onboard_ui.c'));
  assert(m1.includes('if(!saved) c.op=4;'));assert(m1.includes('Failed saved/manual Wi-Fi must leave a usable setup portal'));
  console.log('PASS: 20 invalid address cases, DHCP/static form, UTF-8 limits, encoded credentials, save failure/retry, scan safety, five firmware copies, NVS/DNS/IP integration and M1 SoftAP fallback.');
}
run().catch(e=>{console.error(e);process.exitCode=1;});

// Real C policy/control flow with host mocks; no firmware build or live network.
const fs=require('fs'),path=require('path'),vm=require('vm'),assert=require('node:assert/strict');
const root=path.resolve(__dirname,'..'),read=p=>fs.readFileSync(path.join(root,p),'utf8');
const runtime=read('main/m3_runtime.c'),relay=read('main/cloud_relay.c'),ui=read('main/m3_ui.c');
function body(src,name){const m=new RegExp('\\b'+name+'\\([^)]*\\)\\s*\\{').exec(src);assert(m,name);
 let i=m.index+m[0].length,start=i,depth=1;while(depth){if(src[i]==='{')depth++;if(src[i]==='}')depth--;i++;}return src.slice(start,i-1);}
const c={M3_WEB_STOP_DOMAIN:0,M3_WEB_STOP_ALL:1,M3_WEB_START_DOMAIN:2,M3_WEB_START_ALL:3,
 ESP_OK:0,ESP_ERR_INVALID_STATE:259,NET_MODE_IDLE:0,
 web_pending:false,web_request:-1,web_paused:false,domain_paused:false,mutation_pending:false,
 state:{initialized:true},restarting:false,sd:true,sdBusy:false,running:true,cloud:true,mode:3,startError:0,cloudError:0,
 events:[],m3_preferences_restarting:()=>c.restarting,
 claim:()=>{if(c.web_pending)return false;c.web_pending=true;return true;},
 sd_media_ready:()=>c.sd,sd_media_admin_lock:()=>{c.events.push('lock');return !c.sdBusy;},
 sd_media_admin_unlock:()=>c.events.push('unlock'),
 cloud_relay_set_enabled:value=>{c.cloud=value;c.events.push('cloud:'+value);},
 web_server_stop:()=>{c.running=false;c.events.push('stop');},web_server_is_running:()=>c.running,
 web_server_start:()=>{c.events.push('start');if(!c.startError)c.running=true;return c.startError;},
 cloud_relay_start:()=>c.cloudError,cloud_relay_is_online:()=>c.cloud&&c.running,
 network_get_mode:()=>c.mode,publish:next=>c.published={...next}};
vm.createContext(c);
let admission=body(runtime,'m3_runtime_web').replace(/port(?:ENTER|EXIT)_CRITICAL\(&state_lock\);/g,'')
 .replace('atomic_compare_exchange_strong(&web_pending, &expected, true)','claim()')
 .replace('atomic_store(&web_request, action)','web_request=action').replace(/\bbool (\w+)/g,'let $1');
let execute=body(runtime,'web_execute').replaceAll('->','.').replace(/\b(?:esp_err_t|bool) (\w+)/g,'let $1')
 .replace('atomic_load(&mutation_pending)','mutation_pending').replace('atomic_store(&web_pending, false)','web_pending=false');
vm.runInContext('function request(action){'+admission+'}\nfunction execute(next,action){'+execute+'}',c);
const next={web_control_revision:0};
assert(c.request(0));assert(!c.request(1),'one control operation at a time');assert.equal(c.web_request,0);
c.execute(next,0);assert(c.running&&!c.cloud);assert.equal(next.web_policy,1);assert(!c.web_pending);
c.execute(next,2);assert(c.cloud&&c.running);assert.equal(next.web_policy,0);
c.sdBusy=true;c.events=[];c.execute(next,1);assert(c.running&&c.cloud);assert.equal(next.web_control_result,259);assert(!c.events.includes('stop'));
c.sdBusy=false;c.mutation_pending=true;c.execute(next,1);assert(c.running);assert(c.events.includes('unlock'));
c.mutation_pending=false;c.events=[];c.execute(next,1);assert(!c.cloud&&!c.running);assert.equal(next.web_policy,2);
assert(c.events.indexOf('cloud:false')<c.events.indexOf('stop'),'relay admission closes before HTTP');
c.execute(next,2);assert.equal(next.web_control_result,259,'domain resume requires local HTTP');assert(!c.running);
c.mode=0;c.execute(next,3);assert.equal(next.web_control_result,259);assert(!c.running);
c.mode=3;c.startError=5;c.execute(next,3);assert.equal(next.web_control_result,5);assert(c.web_paused);
c.startError=0;c.execute(next,3);assert(c.running&&c.cloud);assert(!c.web_paused&&!c.domain_paused);
for(const action of [-1,4])assert(!c.request(action));c.restarting=true;assert(!c.request(0));
c.restarting=false;c.state.initialized=false;assert(!c.request(0));
assert(body(runtime,'wifi_execute').includes('!web_paused && !web_server_is_running()'),'Wi-Fi retry cannot restart an intentionally stopped server');
assert(body(runtime,'wifi_execute').includes('if (!domain_paused) cloud_relay_start()'));

// CPU counter policy: real elapsed interval, bounded busy share, no invented LP load.
const busy=body(runtime,'core_busy_percent').replace('uint64_t share','let share').replace('(unsigned)share','Math.floor(share)');
vm.runInContext('function busy(idle_delta,elapsed){'+busy+'}',c);
assert.equal(c.busy(0,2000000),100);assert.equal(c.busy(1000000,2000000),50);
assert.equal(c.busy(2000000,2000000),0);assert.equal(c.busy(2100000,2000000),0);assert.equal(c.busy(0,0),0);
assert(runtime.includes('esp_ipc_call_blocking(core, read_idle_counter'));
assert(runtime.includes('storage_capacity_read(true, &next.sd_capacity)'));
assert(ui.includes('snapshot.cpu_busy[0]'));assert(ui.includes('snapshot.lp_configured ? T_LP_UNMEASURED : T_LP_UNUSED'));
const services=body(runtime,'services');
let capacity=services.slice(services.indexOf('            if (!next.sd) {'),services.indexOf('            bool locked = !browse.sd'));
assert(capacity.includes('next.sd_capacity_stale = true'));
capacity=capacity.replaceAll('memset(&next.sd_capacity, 0, sizeof(next.sd_capacity))','Object.assign(next.sd_capacity,{total:0,used:0,reserved:0,error:0})')
 .replaceAll('&next.sd_capacity','next.sd_capacity');
c.storage_capacity_read=(sd,out)=>{assert(sd);Object.assign(out,{total:64000,used:8000,error:0});};
vm.runInContext('function refresh_capacity(next){'+capacity+'}',c);
let card={sd:true,sd_capacity:{total:0,used:0,error:0},sd_capacity_stale:false};
c.sdBusy=false;c.refresh_capacity(card);assert.equal(card.sd_capacity.used,8000);assert(!card.sd_capacity_stale);
c.sdBusy=true;c.refresh_capacity(card);assert.equal(card.sd_capacity.used,8000);assert(card.sd_capacity_stale,'busy SD preserves last measured capacity');
card.sd=false;c.refresh_capacity(card);assert.equal(card.sd_capacity.total,0);assert.equal(card.sd_capacity.error,259);
card.sd=true;c.refresh_capacity(card);assert(card.sd_capacity_stale);assert.equal(card.sd_capacity.error,259,'first measurement blocked is not empty storage');
c.sdBusy=false;c.refresh_capacity(card);assert.equal(card.sd_capacity.error,0);assert(!card.sd_capacity_stale);

// Disabling publication takes effect in both status and admission, even during TLS polling.
assert(body(relay,'cloud_relay_is_online').includes('cloud_relay_enabled()'));
assert(body(relay,'cloud_relay_status').includes('if (!cloud_relay_enabled()) return "stopped"'));
const exchange=body(relay,'exchange');assert(exchange.indexOf('!cloud_relay_enabled()')<exchange.indexOf('esp_http_client_perform(client)'));
assert(body(relay,'serve_job').includes('if (!cloud_relay_enabled() || !web_server_is_running()) return false;'));
assert(body(relay,'serve_job').includes('if (!cloud_relay_enabled() || https_memory_exclusive_pending()'));
assert(body(relay,'relay_task').includes('if (!cloud_relay_enabled()) {'));

// Stop user work cooperatively. Recorder uses finish/save instead of cancel/discard.
const u={index:0,radio_pending:true,tube_pending:true,ytm_pending:true,ytm_was_active:true,ytm_queue_count:10,
 calls:[],recording:false,shade_note:{},M3_WEB_STOP_ALL:1,T_SERVICE_STOPPING:1,T_SERVICE_BUSY:2,
 lv_event_get_user_data:event=>event.index,m3_recorder_state:out=>out.busy=u.recording,
 m3_recorder_finish:()=>u.calls.push('save-recording'),m3_music_stop:()=>u.calls.push('music'),
 m3_audio_stop:()=>u.calls.push('audio'),voice_cancel:()=>u.calls.push('voice'),
 m3_agent_cancel:clear=>{assert(!clear);u.calls.push('agent');},
 m3_runtime_web:action=>{assert.equal(action,1);u.calls.push('web');return true;},set_text:()=>{},tr:x=>x,NULL:null};
vm.createContext(u);vm.runInContext('function stop(event){'+body(ui,'service_stop')
 .replace('unsigned index = (unsigned)(uintptr_t)','let index = ')
 .replace('m3_recorder_state_t recorder;','let recorder={};').replace('&recorder','recorder')
 .replace('bool accepted','let accepted')+'}',u);
u.stop({index:1});assert.deepEqual(u.calls,['music','audio']);assert.equal(u.ytm_queue_count,0);assert(!u.ytm_pending&&!u.ytm_was_active);
u.calls=[];u.recording=true;u.stop({index:1});assert.deepEqual(u.calls,['save-recording']);
for(const [index,name] of [[2,'voice'],[4,'web'],[5,'agent']]){u.calls=[];u.stop({index});assert.deepEqual(u.calls,[name]);}
console.log('PASS: domain-only/all HTTP stop and recovery, duplicate admission, upload/mutation busy guards, Wi-Fi restart suppression, CPU math, relay pause gates, service cancellation and recording preservation. Host mocks only.');

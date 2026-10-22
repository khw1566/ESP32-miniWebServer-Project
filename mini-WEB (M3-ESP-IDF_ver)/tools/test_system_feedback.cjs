// Execute actual C settings/routing/queue policies with host mocks. No compiler/codec.
const fs=require('fs'),path=require('path'),vm=require('vm'),assert=require('node:assert/strict');
const root=path.resolve(__dirname,'..'),read=p=>fs.readFileSync(path.join(root,p),'utf8');
const source=read('main/m3_feedback.c'),audio=read('main/m3_audio.c');
function body(name,text=source){const m=new RegExp('\\b'+name+'\\([^)]*\\)\\s*\\{').exec(text);assert(m,name);
 let pos=m.index+m[0].length,start=pos,depth=1;while(depth){if(text[pos]==='{')depth++;if(text[pos]==='}')depth--;pos++;}return text.slice(start,pos-1);}
const defaults=()=>({version:1,boot_enabled:1,boot_volume:45,effects_enabled:1,effects_volume:35,guidance_enabled:1,guidance_volume:55,guidance_mode:0});
function context(){
 const c={current:defaults(),revision:0,dirty:false,next_save:0,now:0,cues:[],ready:true,playing:false,volume:40,headless:false,restarting:false,
  ESP_OK:0,NVS_READWRITE:1,pdTRUE:1,M3_CUE_BOOT:0,M3_CUE_TAP:1,M3_CUE_SAVED:6,M3_CUE_COUNT:26,NULL:null,m3_i18n_language:()=>0,
  fail:false,stored:null,writes:0,warnings:0,lock:{},
  portENTER_CRITICAL:()=>{},portEXIT_CRITICAL:()=>{},esp_timer_get_time:()=>c.now,
  memcmp:(a,b)=>JSON.stringify(a)===JSON.stringify(b)?0:1,
  m3_preferences_restarting:()=>c.restarting,m3_preferences_web_active:()=>c.headless,
  m3_audio_state:out=>Object.assign(out,{ready:c.ready,playing:c.playing,volume:c.volume}),
  nvs_open:()=>0,nvs_set_blob:(_h,key,p)=>{assert.equal(key,'sound_settings');c.writes++;c.candidate={...p};return c.fail?1:0;},
  nvs_commit:()=>{if(c.fail)return 1;c.stored={...c.candidate};return 0;},nvs_close:()=>{},
  ESP_LOGW:()=>c.warnings++,esp_err_to_name:()=>'',
  xQueueSend:(q,p)=>{if(q.length>=4)return 0;q.push({...p});return 1;},
  xQueueReceive:(q,p)=>{if(!q.length)return 0;Object.assign(p,q.shift());return 1;},xQueueReset:q=>q.length=0};
 function translate(text){return text.replaceAll('->','.').replace(/\b(?:unsigned|bool|esp_err_t|int64_t) (\w+)/g,'let $1')
  .replaceAll('2000000','2000000').replaceAll('&lock','lock').replaceAll('&p','p').replaceAll('&state','state')
  .replaceAll('&current','current').replaceAll('&nvs','nvs').replace('sizeof(current)','8').replace('sizeof(p)','8')
  .replace('nvs_handle_t nvs;','let nvs={};').replace('m3_feedback_settings_t p;','let p={};')
  .replace('m3_audio_state_t state;','let state={};').replace('pending_t p;','let p={};')
  .replace('*out = current','Object.assign(out,current)').replace('current = *p','current = {...p}')
  .replace('pending_t p = {cue, language, esp_timer_get_time() + 3000000};','let p = {cue,language,expires:esp_timer_get_time()+3000000};')
  .replace('*job = (m3_feedback_job_t){cue, language, volume, channel, channel == 2 && !p.guidance_mode};',
   'Object.assign(job,{cue,language,volume,channel,speech:channel==2&&!p.guidance_mode});');}
 vm.createContext(c);
 for(const [name,args] of [['m3_feedback_valid','p'],['m3_feedback_get','out'],['m3_feedback_update','p'],['m3_feedback_poll',''],
  ['resolve','cue,language,job'],['m3_feedback_emit','cue,language'],['m3_feedback_take','job'],['m3_feedback_clear','']]){
  vm.runInContext('function '+name+'('+args+'){'+translate(body(name))+'}',c);
 }
 return c;
}
let c=context();assert(c.m3_feedback_valid(defaults()));
for(const change of [{version:0},{boot_enabled:2},{effects_enabled:2},{guidance_enabled:2},{guidance_mode:2},
 {boot_volume:101},{effects_volume:101},{guidance_volume:101}])assert(!c.m3_feedback_valid({...defaults(),...change}));
for(const vol of [0,1,100])assert(c.m3_feedback_valid({...defaults(),boot_volume:vol,effects_volume:vol,guidance_volume:vol}));
let job={};assert(c.resolve(0,0,job));assert.equal(job.channel,0);assert.equal(job.volume,45);assert(!job.speech);
assert(c.resolve(1,0,job));assert.equal(job.channel,1);assert.equal(job.volume,35);
assert(c.resolve(3,2,job));assert.equal(job.channel,2);assert.equal(job.language,2);assert(job.speech);assert.equal(job.volume,55);
c.current.guidance_mode=1;c.current.effects_enabled=0;assert(c.resolve(3,1,job));assert(!job.speech);assert.equal(job.volume,55,'accessibility replacement keeps guidance volume even if ordinary effects are off');
c.current.guidance_enabled=0;assert(!c.resolve(3,1,job));c.current.effects_enabled=1;assert(c.resolve(3,1,job));assert.equal(job.volume,35);
c.current.boot_enabled=0;assert(!c.resolve(0,0,job));
c=context();assert(c.m3_feedback_update({...defaults(),boot_volume:10}));c.m3_feedback_poll();assert.equal(c.stored.boot_volume,10);assert(!c.dirty);
c.m3_feedback_update({...defaults(),guidance_mode:1});c.fail=true;c.now=2000000;c.m3_feedback_poll();assert(c.dirty);assert.equal(c.stored.guidance_mode,0);
c.fail=false;c.now=4000000;c.m3_feedback_poll();assert(!c.dirty);assert.equal(c.stored.guidance_mode,1);
c=context();for(const flag of ['headless','restarting','playing']){c[flag]=true;c.m3_feedback_emit(3,0);assert.equal(c.cues.length,0);c[flag]=false;}
c.volume=0;c.m3_feedback_emit(3,0);assert.equal(c.cues.length,0);c.volume=40;
c.m3_feedback_emit(26,0);c.m3_feedback_emit(3,3);assert.equal(c.cues.length,0);
c.m3_feedback_emit(3,2);assert.equal(c.cues.length,1);assert(c.m3_feedback_take(job));assert.equal(job.language,2);
c.m3_feedback_emit(3,0);c.now=3000001;assert(!c.m3_feedback_take(job),'stale cues must not play after long media jobs');
for(let i=0;i<10;i++)c.m3_feedback_emit(1,0);assert.equal(c.cues.length,4);c.m3_feedback_clear();assert.equal(c.cues.length,0);
const volume={volume:40,feedback_volume:55,UINT_MAX:4294967295,atomic_load:v=>v};vm.createContext(volume);
vm.runInContext('function level(){'+body('output_volume',audio).replaceAll('&volume','volume').replace('unsigned master','let master')+'}',volume);
assert.equal(volume.level(),55);volume.volume=0;assert.equal(volume.level(),0);volume.volume=40;volume.feedback_volume=volume.UINT_MAX;assert.equal(volume.level(),40);
const run=body('m3_feedback_run');
assert(run.includes('result == 0 && !out->cancelled()'),'no second codec open after an output failure');
assert(!source.includes('esp_codec_dev_'));assert(!source.includes('xTaskCreate'));
assert(audio.includes('uxQueueMessagesWaiting(requests)'));assert(audio.includes('if (atomic_load(&feedback_active)) interrupt_feedback();'));
const interrupt=body('interrupt_feedback',audio);
for(const cancel of ['m3_voice_cancel','m3_recorder_cancel','m3_gallery_audio_cancel'])assert(!interrupt.includes(cancel));
// Start a prepared voice job during a cue; cue interruption must not cancel it.
const dispatch={requests:[],current:{ready:true,playing:true},feedback_active:true,cancel_epoch:0,voice_valid:false,
 pdTRUE:1,PLAY_VOICE:5,atomic_load:x=>x,m3_preferences_restarting:()=>false,
 m3_audio_state:out=>Object.assign(out,dispatch.current),
 m3_voice_prepare:()=>{dispatch.voice_valid=true;return 123;},m3_voice_cancel:()=>dispatch.voice_valid=false,
 m3_feedback_clear:()=>{},xQueueReset:q=>q.length=0,xQueueSend:(q,p)=>{q.push({...p});return 1;},memset:()=>{}};
vm.createContext(dispatch);
vm.runInContext('function interrupt_feedback(){'+interrupt.replace('atomic_fetch_add(&cancel_epoch, 1)','cancel_epoch++')+'}',dispatch);
let enqueue=body('enqueue_app',audio).replaceAll('&feedback_active','feedback_active').replaceAll('&cancel_epoch','cancel_epoch')
 .replace('m3_audio_state_t current;','let current={};').replaceAll('&current','current')
 .replace('request_t request = {.kind = kind, .epoch = atomic_load(cancel_epoch), .speed = speed, .aac = aac};',
  'let request={kind,epoch:atomic_load(cancel_epoch),speed,aac};')
 .replace('snprintf(request.name, sizeof(request.name), "%s", text);','request.name=text;')
 .replace('bool accepted','let accepted').replaceAll('&request','request').replaceAll('sizeof(request)','0');
vm.runInContext('function enqueue_app(kind,text,speed,aac){'+enqueue+'}',dispatch);
let voice=body('m3_audio_voice',audio).replace('m3_audio_state_t current;','let current={};').replaceAll('&current','current')
 .replaceAll('&feedback_active','feedback_active').replace('unsigned job','let job');
vm.runInContext('function start_voice(){'+voice+'}',dispatch);
assert(dispatch.start_voice());assert(dispatch.voice_valid);assert.equal(dispatch.requests[0].speed,123);assert.equal(dispatch.cancel_epoch,1);
assert(read('main/m3_runtime.c').includes('m3_feedback_poll();'));
console.log('PASS: actual C channel validation, independent volume, accessibility replacement, persisted settings retry, headless/mute/busy admission, bounded/expired queues, user cancellation and single codec ownership. Host mocks only.');

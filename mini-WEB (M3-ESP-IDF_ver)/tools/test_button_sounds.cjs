// Real C PCM synthesis and codec sequencing under host mocks. No speaker/I2S test.
const fs=require('fs'),path=require('path'),vm=require('vm'),assert=require('node:assert/strict');
const root=path.resolve(__dirname,'..'),read=p=>fs.readFileSync(path.join(root,p),'utf8');
const sound=read('main/include/m3_sound.h'),audio=read('main/m3_audio.c');
function body(src,name){const m=new RegExp('\\b'+name+'\\([^)]*\\)\\s*\\{').exec(src);assert(m,name);
 let i=m.index+m[0].length,start=i,depth=1;while(depth){if(src[i]==='{')depth++;if(src[i]==='}')depth--;i++;}return src.slice(start,i-1);}
const c={sinf:Math.sin};vm.createContext(c);
const sample=body(sound,'m3_sound_sample').replace(/\b(?:unsigned|float) (\w+)/g,'let $1')
 .replace(/(\d+(?:\.\d+)?)f\b/g,'$1').replace('return (int16_t)(wave * gain * 1800.0);','return Math.trunc(wave * gain * 1800.0) || 0;');
vm.runInContext('function m3_sound_frames(sound){'+body(sound,'m3_sound_frames')+'}\nfunction sample(sound,index){'+sample+'}',c);
assert.equal(c.m3_sound_frames(0),0);assert.equal(c.sample(0,0),0);assert.equal(c.m3_sound_frames(4),0);
for(const style of [1,2,3]){
 const n=c.m3_sound_frames(style),pcm=Array.from({length:n},(_,i)=>c.sample(style,i));
 assert(n>=1920&&n<=8640);assert.equal(pcm[0],0);assert.equal(pcm.at(-1),0);assert.equal(c.sample(style,n),0);
 assert(Math.max(...pcm.map(Math.abs))<=1800,'bounded, low-level signed PCM');
 assert(Math.abs(pcm.reduce((a,b)=>a+b,0)/n)<30,'no sustained DC offset');
 assert(Math.sqrt(pcm.reduce((a,b)=>a+b*b,0)/n)>200,'non-silent effect');
 assert(Math.max(...pcm.slice(1).map((v,i)=>Math.abs(v-pcm[i])))<400,'smooth attack and release');
 if(style===3)assert(pcm.slice(3840,4800).every(v=>v===0),'two notes have a quiet gap');
}
assert(Array.from({length:512},(_,i)=>c.sample(1,i)!==c.sample(2,i)).some(Boolean),'effect waveforms differ');

// Execute open_rate up to diagnostic logging, retaining real priming/volume calls.
const d={output_active:false,volume:40,applied_volume:0,playback_rate:0,UINT_MAX:0xffffffff,
 M3_AMP_ENABLE:11,ESP_OK:0,AUDIO_DMA_DESCS:6,AUDIO_DMA_FRAMES:240,codec:{},tx:'tx',rx:'rx',
 amp:0,muted:true,events:[],primeFail:false,codecFail:false,
 atomic_load:value=>value,gpio_set_level:(_pin,level)=>{d.amp=Number(level);d.events.push('amp:'+Number(level));return 0;},
 esp_codec_dev_set_out_mute:(_codec,mute)=>{d.muted=mute;d.events.push('mute:'+mute);return 0;},
 esp_codec_dev_set_out_vol:(_codec,v)=>{assert(v<=100);d.events.push('volume:'+v);return 0;},
 i2s_channel_enable:()=>0,i2s_channel_disable:()=>0,
 esp_codec_dev_open:(_codec,format)=>{assert.equal(d.amp,0);assert(d.muted);d.events.push('open');return d.codecFail?1:0;},
 esp_codec_dev_write:(_codec,pcm,n)=>{assert.equal(d.amp,0);assert(d.muted);assert.equal(n,960);assert([...pcm].every(v=>v===0));d.events.push('silence');return d.primeFail?1:0;},
 esp_codec_dev_close:()=>{assert.equal(d.amp,0);d.events.push('close');return 0;},
 codec_result:(_label,value)=>value===0,ESP_LOGE:()=>{},esp_err_to_name:String,
 pdMS_TO_TICKS:x=>x,vTaskDelay:()=>d.events.push('settle')};
function translate(code){return code.replace(/\/\*[\s\S]*?\*\//g,'').replace(/atomic_load\(&volume\)/g,'volume')
 .replace(/\b(?:unsigned|bool|esp_err_t) (\w+)/g,'let $1');}
const open=body(audio,'open_rate').split('    ESP_LOGI(TAG, "Output:')[0]
 .replace('esp_codec_dev_sample_info_t format = {.sample_rate = rate, .channel = 2, .bits_per_sample = 16};','let format={sample_rate:rate,channel:2,bits_per_sample:16};')
 .replace('int16_t silence[AUDIO_DMA_FRAMES * 2] = {0};','let silence=new Int16Array(AUDIO_DMA_FRAMES*2);')
 .replaceAll('sizeof(silence)','silence.byteLength').replaceAll('&format','format');
vm.createContext(d);vm.runInContext('function apply_volume(){'+translate(body(audio,'apply_volume'))+'}\nfunction open(rate){'+translate(open)+'return true;}\nfunction close(){'+translate(body(audio,'close_codec'))+'}',d);
assert(d.open(48000));assert.equal(d.playback_rate,48000);assert.equal(d.amp,1);assert(!d.muted);
assert.equal(d.events.filter(e=>e==='silence').length,6);assert(d.events.indexOf('settle')<d.events.indexOf('amp:1'));
d.close();assert.equal(d.amp,0);assert(d.muted);assert(!d.output_active);
d.volume=0;assert(d.open(48000));assert.equal(d.amp,0);assert(d.muted);
d.volume=80;assert(d.apply_volume());assert.equal(d.amp,1);assert(!d.muted);
d.close();d.primeFail=true;assert(!d.open(48000));assert.equal(d.amp,0);d.close();
d.primeFail=false;d.codecFail=true;assert(!d.open(48000));assert.equal(d.amp,0);d.close();

// UI effects have a separate best-effort slot; real media queue always takes priority.
const e={sound:1,ready:true,playing:false,volume:40,queued:0,restarting:false,now:1000,
 requests:{},pending_effect:0,effect_requested_at:0,
 m3_preferences_sound:()=>e.sound,m3_audio_state:out=>Object.assign(out,{ready:e.ready,playing:e.playing,volume:e.volume}),
 m3_preferences_restarting:()=>e.restarting,uxQueueMessagesWaiting:()=>e.queued,esp_timer_get_time:()=>e.now*1000};
let click=body(audio,'m3_audio_click').replace('unsigned sound','let sound').replace('m3_audio_state_t current;','let current={};')
 .replace('&current','current').replaceAll('current.','current.')
 .replace('atomic_store(&effect_requested_at, (uint32_t)(esp_timer_get_time() / 1000))','effect_requested_at=esp_timer_get_time()/1000')
 .replace('atomic_store(&pending_effect, sound)','pending_effect=sound');
vm.createContext(e);vm.runInContext('function click(){'+click+'}',e);assert(e.click());assert.equal(e.pending_effect,1);
for(const patch of [{sound:0},{ready:false},{playing:true},{volume:0},{queued:1},{restarting:true}]){
 const saved={sound:e.sound,ready:e.ready,playing:e.playing,volume:e.volume,queued:e.queued,restarting:e.restarting};
 Object.assign(e,patch);assert(!e.click(),JSON.stringify(patch));Object.assign(e,saved);
}
const effect=body(audio,'play_effect');assert(effect.includes('uxQueueMessagesWaiting(requests)'));
assert(effect.includes('esp_codec_dev_write(codec, pcm, count * 4)'),'effects bypass software boost above 100');
assert(audio.includes('(uint32_t)(now - atomic_load(&effect_requested_at)) <= 200'),'stale clicks expire');
assert(audio.includes('.pa_pin = -1'),'codec cannot enable speaker during init');
console.log('PASS: actual C effect waveforms, bounded peak/DC/edges, dual-note gap, mute/silence/PA sequencing, mute restore, failure cleanup, media priority and stale-click expiry. Host mocks only; analogue sound needs board verification.');

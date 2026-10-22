// Host execution of codec input and DMA drain policies. No physical codec/I2S validation.
const fs=require('fs'),path=require('path'),assert=require('node:assert/strict'),vm=require('vm');
const root=path.resolve(__dirname,'..'),source=fs.readFileSync(path.join(root,'main/m3_audio.c'),'utf8');
function body(name){const m=new RegExp('\\b'+name+'\\([^)]*\\)\\s*\\{').exec(source);assert(m,name);
 let i=m.index+m[0].length,start=i,depth=1;while(depth){if(source[i]==='{')depth++;if(source[i]==='}')depth--;i++;}return source.slice(start,i-1);}
const input=body('capture_read').replace('int16_t stereo[320];','let stereo=new Int16Array(320);')
 .replace('unsigned energy[2] = {0};','let energy=[0,0];')
 .replace(/\b(?:unsigned|int) (\w+)/g,'let $1');
function capture(slot,{cancel=false,readFailure=false,lateCancel=false}={}){
 const c={codec:{},cancel,reads:0,cancelled:()=>c.cancel,codec_result:(_label,ok)=>ok,
 esp_codec_dev_read:(_codec,b,n)=>{c.reads++;assert.equal(n,640);
  for(let i=0;i<160;i++){b[i*2+slot]=i%2?-24000:24000;b[i*2+1-slot]=0;}
  if(lateCancel)c.cancel=true;return !readFailure;}};
 vm.createContext(c);vm.runInContext('function read(mono,samples){'+input+'}',c);
 const pcm=new Int16Array(160),ok=c.read(pcm,160);return {c,pcm,ok};
}
for(const slot of [0,1]){const {pcm,ok}=capture(slot);assert(ok);assert.equal(pcm[0],24000);assert.equal(pcm[1],-24000);}
assert(!capture(0,{cancel:true}).ok);assert.equal(capture(0,{cancel:true}).c.reads,0);
assert(!capture(0,{readFailure:true}).ok);assert(!capture(0,{lateCancel:true}).ok);
let drain=body('drain_output').replace('int16_t silence[AUDIO_DMA_FRAMES * 2] = {0};','let silence=new Int16Array(AUDIO_DMA_FRAMES*2);')
 .replaceAll('sizeof(silence)','silence.byteLength').replace(/\bunsigned (\w+)/g,'let $1').replace(/\b(\d+)U\b/g,'$1');
function finish(options={}){
 const c={AUDIO_DMA_DESCS:6,AUDIO_DMA_FRAMES:240,playback_rate:48000,writes:0,waits:0,cancel:false,
 cancelled:()=>c.cancel,pdMS_TO_TICKS:x=>x,
 write_pcm:(pcm,n)=>{assert.equal(n,960);assert([...pcm].every(v=>v===0));return ++c.writes!==options.failAt;},
 vTaskDelay:()=>{c.waits++;if(options.cancel)c.cancel=true;}};
 vm.createContext(c);vm.runInContext('function finish(){'+drain+'}',c);return {c,ok:c.finish()};
}
let result=finish();assert(result.ok);assert.equal(result.c.writes,6);assert(result.c.waits>0);
result=finish({failAt:3});assert(!result.ok);assert.equal(result.c.waits,0);
assert(!finish({cancel:true}).ok);
const opening=body('open_rate');
assert(opening.indexOf('i2s_channel_enable(tx)')<opening.indexOf('esp_codec_dev_open(codec, &format)'));
assert(opening.includes('esp_codec_dev_set_out_mute(codec, true)'));
assert(opening.indexOf('gpio_set_level(M3_AMP_ENABLE, 0)') < opening.indexOf('esp_codec_dev_open(codec, &format)'));
assert(opening.indexOf('"prime silence"') < opening.indexOf('if (!apply_volume())'));
assert(body('apply_volume').includes('esp_codec_dev_set_out_mute(codec, requested == 0)'));
assert(source.includes('.pa_pin = -1'),'PA must remain off during codec init and clock changes');
assert(body('close_codec').indexOf('gpio_set_level(M3_AMP_ENABLE, 0)') < body('close_codec').indexOf('esp_codec_dev_close(codec)'));
assert(opening.includes('applied_volume = UINT_MAX'),'playback restores volume after microphone capture');
assert(source.includes('I2S_MCLK_MULTIPLE_256'));assert(source.includes('.mclk_div = 256'));
assert(body('write_pcm').includes('apply_volume()'));assert(body('play_file').includes('ok = drain_output()'));
assert(!source.includes('play_test('));assert(!source.includes('m3_audio_test('));
console.log('PASS: mono capture from either stereo slot without attenuation, read/cancel errors, output unmute/volume restoration and DMA drain/failure/cancellation. Test-tone backend removed. Host mocks only.');

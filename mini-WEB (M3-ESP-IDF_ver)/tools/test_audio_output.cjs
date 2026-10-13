// Host execution of the actual test-tone body with device operations mocked.
// Does not validate I2S clocks, codec registers or physical speaker output.
const fs=require('fs'),path=require('path'),assert=require('assert'),vm=require('vm');
const root=path.resolve(__dirname,'..');
const source=fs.readFileSync(path.join(root,'main/m3_audio.c'),'utf8');
function body(name){
 const match=new RegExp('\\b'+name+'\\([^)]*\\)\\s*\\{').exec(source);assert(match);
 let depth=1,start=match.index+match[0].length,end=start;
 while(depth){if(source[end]==='{')depth++;if(source[end]==='}')depth--;end++;}
 return source.slice(start,end-1);
}
const blocks=Number(/TEST_BLOCKS = (\d+)/.exec(source)[1]);
const test=body('play_test')
 .replace('atomic_load(&volume)','volume')
 .replace('int16_t samples[960];','let samples = new Int16Array(960);')
 .replace(/\b(?:bool|unsigned|int32_t)\s+(\w+)/g,'let $1')
 .replace(/\((?:int16_t|int32_t)\)/g,'')
 .replace(/sizeof\(samples\)/g,'samples.byteLength');
function run({volume=40,failAt=-1,open=true,drain=true}={}){
 const writes=[],steps=[],messages=[];
 const ctx={volume,TEST_BLOCKS:blocks,TAG:'test',codec:{},ESP_LOGI:()=>{},
  open_rate:rate=>{assert.equal(rate,48000);steps.push('open');return open;},
  write_pcm:(samples,bytes)=>{assert.equal(bytes,samples.byteLength);if(writes.length===failAt)return false;writes.push([...samples]);return true;},
  drain_output:()=>{steps.push('drain');return drain;},
  esp_codec_dev_close:()=>steps.push('close'),cancelled:()=>!drain,
  message:(...m)=>messages.push(m)};
 vm.createContext(ctx);vm.runInContext('function play_test(){'+test+'}\nplay_test();',ctx);
 return {writes,steps,messages};
}
let result=run();assert.deepEqual(result.steps,['open','drain','close']);
assert.equal(result.writes.length*480/48000,2,'test must last 2 seconds');
assert.equal(result.writes.length*960*2,384000);
let peak=0;for(const block of result.writes)for(let i=0;i<block.length;i+=2){assert.equal(block[i],block[i+1]);peak=Math.max(peak,Math.abs(block[i]));}
assert.equal(peak,6000);assert.equal(result.messages.at(-1)[0],'Speaker test complete');
result=run({volume:0});assert.equal(result.writes.length,0);assert.equal(result.steps.length,0);
result=run({open:false});assert.deepEqual(result.steps,['open','close']);
result=run({failAt:3});assert.equal(result.writes.length,3);assert.deepEqual(result.steps,['open','close']);
assert.equal(result.messages.at(-1)[2],0);
result=run({drain:false});assert.equal(result.messages.at(-1)[2],0,'stop while draining must not report complete');
const opening=body('open_rate');
assert(opening.indexOf('i2s_channel_enable(tx)')<opening.indexOf('esp_codec_dev_open(codec, &format)'));
assert(opening.includes('esp_codec_dev_set_out_mute(codec, false)'));
assert(source.includes('I2S_MCLK_MULTIPLE_256'));assert(source.includes('.mclk_div = 256'));
assert(body('write_pcm').includes('apply_volume()'));
assert(body('apply_volume').includes('requested == applied_volume'));
assert(body('play_file').includes('ok = drain_output()'));
console.log('PASS: actual 2-second stereo test waveform, zero volume, open/write failure, DMA drain ordering/cancellation, explicit clock and unmute setup.');

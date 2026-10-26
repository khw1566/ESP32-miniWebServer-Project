// Execute source policies with transport mocks. No firmware or live audio test.
const fs=require('fs'),path=require('path'),vm=require('vm'),assert=require('assert');
const root=path.resolve(__dirname,'..'),read=p=>fs.readFileSync(path.join(root,p),'utf8').replace(/\r\n/g,'\n');
const source=read('main/m3_radio.c'),ui=read('main/m3_ui.c');
function body(name,text=source){
 const match=new RegExp('\\b'+name+'\\([^)]*\\)\\s*\\{').exec(text);assert(match,name);
 let start=match.index+match[0].length,end=start,depth=1;
 while(depth){if(text[end]==='{')depth++;if(text[end]==='}')depth--;end++;}
 return text.slice(start,end-1);
}
const limits={RADIO_BUFFER_BYTES:512*1024,RADIO_PREBUFFER_BYTES:96*1024,RADIO_READ_BYTES:4096,RADIO_PCM_BYTES:256*1024,RADIO_PCM_PREBUFFER_MS:750,RADIO_PCM_BLOCK_BYTES:1920};
for(const [name,value] of Object.entries(limits)){
 const definition=new RegExp(name+' = ([0-9 *]+)').exec(source);assert(definition,name);
 assert.equal(vm.runInNewContext(definition[1]),value);
}
const policy={...limits};vm.createContext(policy);
for(const [name,args] of [['radio_buffer_ready','available,finished,target'],['radio_prebuffer_bytes','bitrate'],['radio_pcm_target','rate,milliseconds']])
 vm.runInContext('function '+name+'('+args+'){'+body(name).replace(/\bunsigned (\w+)/g,'let $1').replace('~3U','~3')+'}',policy);
assert.equal(policy.radio_prebuffer_bytes(128000),96000);
assert.equal(policy.radio_prebuffer_bytes(64000),48000);
assert.equal(policy.radio_prebuffer_bytes(0),98304);
assert.equal(policy.radio_prebuffer_bytes(16000),32768);
assert.equal(policy.radio_prebuffer_bytes(512000),262144);
for(const rate of [8000,22050,44100,48000]){
 const target=policy.radio_pcm_target(rate,750);assert.equal(target%4,0);assert(target<=limits.RADIO_PCM_BYTES*3/4);
 assert(!policy.radio_buffer_ready(target-4,false,target));assert(policy.radio_buffer_ready(target,false,target));
 assert(policy.radio_buffer_ready(4,true,target));assert(!policy.radio_buffer_ready(0,true,target));
}
// Discrete two-ring timeline uses the actual source admission/PCM policies.
// HTTP is delivered in 250ms bursts; decoder stalls independently of HTTP.
function timeline(networkStallMs,decodeStallMs=0){
 const rate=44100,pcmPerTick=rate*4/100,ratio=rate*4/16000;
 let compressed=0,pcm=0,started=false,playing=false,gaps=0,resumed=0;
 for(let tick=0;tick<3000;tick++){
  const ms=tick*10,netStalled=ms>=12000&&ms<12000+networkStallMs;
  if(tick%25===0&&!netStalled)compressed=Math.min(limits.RADIO_BUFFER_BYTES,compressed+4000);
  if(!started&&policy.radio_buffer_ready(compressed,false,policy.radio_prebuffer_bytes(128000)))started=true;
  if(started&&!(ms>=10000&&ms<10000+decodeStallMs)){
   const bytes=Math.min(compressed,(limits.RADIO_PCM_BYTES-pcm)/ratio,16000);
   compressed-=bytes;pcm+=bytes*ratio;
  }
  if(started&&!playing&&policy.radio_buffer_ready(pcm,false,policy.radio_pcm_target(rate,gaps?1500:750))){playing=true;if(gaps)resumed++;}
  if(playing){if(pcm+0.001<pcmPerTick){playing=false;gaps++;}else pcm-=pcmPerTick;}
 }
 return {gaps,resumed,pcm,compressed};
}
assert.equal(timeline(5000,300).gaps,0,'5s network gap and independent 300ms decoder delay fit both reserves');
const long=timeline(10000);assert(long.gaps>0);assert(long.resumed>0,'long outage refills PCM and resumes; no requirement for full compressed reserve');
// Execute the actual receive task with partial stream-buffer sends and stop.
const transport={...limits,bytes:Buffer.from(Array.from({length:999},(_,i)=>i%256)),position:0,written:[],deleted:false,ticks:0,
 rx:{stop:false,done:false,received:0,chunk:Buffer.alloc(4096),stream:{},http:{},icy:{}},
 esp_timer_get_time:()=>transport.ticks+=1000,
 esp_http_client_read:(_,chunk)=>{const n=Math.min(137,transport.bytes.length-transport.position);transport.bytes.copy(chunk,0,transport.position,transport.position+n);transport.position+=n;return n;},
 esp_http_client_is_complete_data_received:()=>transport.position===transport.bytes.length,
 network_get_mode:()=>1,NET_MODE_STA_CONNECTED:1,ESP_ERR_HTTP_EAGAIN:0x7007,atomic_fetch_add:(field,n)=>transport.rx.received+=n,icy_filter:(_,chunk,n)=>n,
 pdMS_TO_TICKS:n=>n,vTaskDelay:()=>{},vTaskDelete:()=>transport.deleted=true,
 xStreamBufferSend:(_,chunk,length)=>{const n=Math.min(47,length);transport.written.push(...chunk.subarray(0,n));return n;}};
let receive=body('radio_receive').replace('radio_rx_t *rx = arg','let rx = arg')
 .replace('int64_t last_data','let last_data').replace('int bytes','let bytes').replace(/\bunsigned (\w+)/g,'let $1')
 .replace('(char *)','').replace('(unsigned)','').replaceAll('rx->','rx.')
 .replaceAll('atomic_load(&rx.stop)','rx.stop').replace('atomic_store(&rx.done, true)','rx.done = true')
 .replace('rx.chunk + offset','rx.chunk.subarray(offset)').replace('atomic_fetch_add(&rx.received, sent)','rx.received += sent').replace('vTaskDelete(NULL)','vTaskDelete(null)');
vm.createContext(transport);vm.runInContext('function receive(arg){'+receive+'}',transport);
transport.receive(transport.rx);assert(transport.rx.done&&transport.deleted);
assert.deepEqual(Buffer.from(transport.written),transport.bytes,'partial sends never drop compressed bytes');
transport.position=0;transport.written=[];transport.rx.stop=false;transport.rx.done=false;
transport.xStreamBufferSend=(_,chunk,length)=>{transport.rx.stop=true;return 0;};
transport.receive(transport.rx);assert(transport.rx.done);assert.equal(transport.position,137,'stop interrupts backpressure loop');
const originalRead=transport.esp_http_client_read,originalSend=transport.xStreamBufferSend;
transport.position=0;transport.written=[];transport.rx.stop=false;transport.rx.done=false;let retries=0;
transport.esp_http_client_read=(...args)=>retries++<3?-transport.ESP_ERR_HTTP_EAGAIN:originalRead(...args);
transport.xStreamBufferSend=(_,chunk,n)=>{transport.written.push(...chunk.subarray(0,n));return n;};
transport.receive(transport.rx);assert.equal(transport.rx.error,'Radio stream ended');assert.deepEqual(Buffer.from(transport.written),transport.bytes);
transport.rx.stop=false;transport.rx.done=false;transport.position=0;
transport.esp_http_client_read=()=>-transport.ESP_ERR_HTTP_EAGAIN;transport.esp_timer_get_time=()=>transport.ticks+=21000000;
transport.receive(transport.rx);assert.equal(transport.rx.error,'Radio receive stalled for 20 seconds');
transport.rx.stop=false;transport.rx.done=false;transport.network_get_mode=()=>0;
transport.receive(transport.rx);assert.equal(transport.rx.error,'Radio Wi-Fi disconnected');
// Execute the actual decoder task with split PCM sends and an input-cache drain.
const decoded={...limits,ESP_AUDIO_ERR_OK:0,position:0,output:[],reads:0,processes:0,deleted:false,
 dec:{stop:false,done:false,format_ready:false,rate:0,rx:{stream:{},done:false,error:'EOF'},stream:{},decoder:{},
  input:Buffer.alloc(4096),pcm:Buffer.alloc(32768),stereo:new Int16Array(32768)},
 esp_timer_get_time:()=>0,pdMS_TO_TICKS:n=>n,vTaskDelete:()=>decoded.deleted=true,
 xStreamBufferBytesAvailable:()=>0,
 xStreamBufferReceive:(_,input)=>{if(decoded.reads++){decoded.dec.rx.done=true;return 0;}input.set([1,2,3,4]);return 4;},
 esp_audio_simple_dec_process:(_,raw,frame)=>{
  decoded.processes++;
  // First frame is cached, consumes zero bytes. Remaining calls consume 2.
  raw.consumed=decoded.processes===1?0:Math.min(2,raw.len);
  frame.decoded_size=4;frame.buffer.writeInt16LE(1234,0);frame.buffer.writeInt16LE(-2345,2);return 0;},
 esp_audio_simple_dec_get_info:(_,info)=>{Object.assign(info,{bits_per_sample:16,channel:1,sample_rate:44100});return 0;},
 xStreamBufferSend:(_,samples,count)=>{const n=Math.min(3,count);decoded.output.push(...samples.subarray(0,n));return n;},
 atomic_load:value=>value,atomic_store:()=>{}};
let decode=body('radio_decode').replace('radio_decode_t *dec = arg','let dec = arg')
 .replace(/\b(?:int64_t|unsigned) (\w+)/g,'let $1').replaceAll('dec->','dec.')
 .replace(/esp_audio_simple_dec_raw_t raw = \{([\s\S]*?)\};/,'let raw = {$1};')
 .replace(/esp_audio_simple_dec_out_t frame = \{([\s\S]*?)\};/,'let frame = {$1};')
 .replace(/\.(buffer|len|eos) =/g,'$1:')
 .replace('esp_audio_err_t result','let result').replace('esp_audio_simple_dec_info_t info = {0};','let info = {};')
 .replace('&raw, &frame','raw, frame').replace('&info','info')
 .replace('uint8_t *samples','let samples').replace('int16_t *mono = (int16_t *)dec.pcm;',
  'let mono = new Int16Array(dec.pcm.buffer, dec.pcm.byteOffset, dec.pcm.length / 2);')
 .replace('(uint8_t *)dec.stereo','Buffer.from(dec.stereo.buffer)')
 .replace('dec.input + offset','dec.input.subarray(offset)').replace('samples + sent','samples.subarray(sent)')
 .replaceAll('dec.rx->','dec.rx.').replace(/atomic_load\(&(dec(?:\.\w+)+)\)/g,'$1')
 .replace(/atomic_store\(&(dec\.\w+), true\)/g,'$1 = true')
 .replace(/goto done;/g,'break decode_loop;').replace('while (!dec.stop)', 'decode_loop: while (!dec.stop)')
 .replace('\ndone:','').replace('vTaskDelete(NULL)','vTaskDelete(null)');
decoded.Buffer=Buffer;vm.createContext(decoded);vm.runInContext('function decode(arg){'+decode+'}',decoded);
decoded.decode(decoded.dec);assert(decoded.dec.done&&decoded.deleted);assert.equal(decoded.processes,3);
const golden=Buffer.alloc(24);for(let i=0;i<3;i++){golden.writeInt16LE(1234,i*8);golden.writeInt16LE(1234,i*8+2);
 golden.writeInt16LE(-2345,i*8+4);golden.writeInt16LE(-2345,i*8+6);}
assert.deepEqual(Buffer.from(decoded.output),golden,'mono expansion and partial PCM writes preserve every byte');
decoded.dec.stop=false;decoded.dec.done=false;decoded.dec.rx.done=false;decoded.reads=0;decoded.processes=0;decoded.output=[];
decoded.xStreamBufferSend=()=>{decoded.dec.stop=true;return 0;};decoded.decode(decoded.dec);assert(decoded.dec.done);
assert.equal(decoded.processes,1,'stop releases decoder blocked on PCM backpressure');
const decodeBody=body('radio_decode');assert(!/out->|out\.|esp_codec_dev_|i2s_/.test(decodeBody),'decoder is not an I2S owner');
assert(source.includes('xTaskCreatePinnedToCore(radio_decode'));
const cleanup=source.slice(source.lastIndexOf('\ndone:'));
assert(cleanup.indexOf('atomic_store(&rx.stop, true)')<cleanup.indexOf('while (decoder_started'));
assert(cleanup.indexOf('while (decoder_started')<cleanup.indexOf('esp_audio_simple_dec_close'));
assert(source.includes('esp_http_client_set_timeout_ms(http, 250)'));
assert(source.includes('if (ps_changed) m3_wifi_performance_release()'));
assert(source.includes('rx=%lu B/s'));
const sink=source.slice(source.indexOf('int64_t last_status = 0, last_stats'));
assert(sink.includes('xStreamBufferReceive(dec.stream, output'));
assert(!sink.includes('if (!xStreamBufferBytesAvailable(rx.stream))'),'compressed empty alone cannot trigger rebuffer');
assert(source.includes('MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT'));
assert(source.indexOf('while (receiver_started && !atomic_load(&rx.done))')<source.indexOf('esp_http_client_cleanup(http)'));
assert(source.includes('xTaskCreatePinnedToCore(radio_receive'));
// Execute station selection, including codec selection and bounds.
const stations=JSON.parse(fs.readFileSync(path.join(root,'../tube-relay/radio_stations.json'),'utf8')).map(s=>({...s,url:s.url||''}));
assert.equal(stations.length,20);assert(stations.filter(s=>s.region===1).length>=6);assert(stations.filter(s=>s.region===2).length>=7);
assert(stations.some(s=>s.name.includes('Classic')));assert(stations.some(s=>s.aac&&s.url.includes('64-aac')));
const catalog=read('main/m3_radio_stations.inc');
for(const s of stations){assert(catalog.includes(JSON.stringify(s.id)));assert(catalog.includes(JSON.stringify(s.url)));}
const picker={radio_stations:stations,radio_url:{},radio_codec:{},marked:null,
 lv_event_get_user_data:event=>event.index,hide_keyboard:()=>{},radio_mark:index=>picker.marked=index,
 lv_textarea_set_text:(_,url)=>picker.url=url,segment_select:(_,codec)=>picker.codec=codec};
vm.createContext(picker);
const cast=source=>source.replace('unsigned index = (unsigned)(uintptr_t)','let index = ').replace(/\(int\)index/g,'index')
 .replace('sizeof(radio_stations) / sizeof(radio_stations[0])','radio_stations.length');
vm.runInContext('function radio_select(index){'+cast(body('radio_select',ui))+'}\nfunction select(event){'+cast(body('radio_preset',ui))+'}',picker);
for(let i=0;i<stations.length;i++){picker.select({index:i});assert.equal(picker.url,stations[i].url);assert.equal(picker.codec,stations[i].aac?1:0);assert.equal(picker.marked,i);}
picker.select({index:99});assert.equal(picker.url,stations.at(-1).url,'out-of-range station is ignored');
const routes={radio_selected:0,radio_transport:{mode:0},radio_codec:{mode:0},radio_url:{},radio_info:{},
 radio_stations:stations.map(s=>({...s,relay:!!s.kbs})),NULL:null,T_RADIO_FAIL:0,T_RADIO_QUEUED:1,
 hide_keyboard:()=>{},segment_value:o=>o.mode,lv_textarea_get_text:()=>routes.url||'',
 m3_audio_radio_preset:(id,profile)=>{routes.sent={id,profile};return routes.accept;},
 m3_audio_radio:(url,aac)=>{routes.sent={url,aac};return routes.accept;},
 lv_label_set_text:(o,s)=>o.text=s,tr:s=>s};
vm.createContext(routes);vm.runInContext('function start(event){'+body('start_radio',ui)
 .replace('(void)event;','').replace(/\bunsigned |\bbool /g,'let ').replace('(unsigned)radio_selected','radio_selected')
 .replace('sizeof(radio_stations) / sizeof(radio_stations[0])','radio_stations.length')+'}',routes);
routes.accept=true;
for(let i=0;i<stations.length;i++)for(let mode=0;mode<4;mode++){
 routes.radio_selected=i;routes.radio_transport.mode=mode;routes.url=stations[i].url;routes.start(null);
 assert.deepEqual(routes.sent,mode||stations[i].kbs?{id:stations[i].id,profile:mode?mode-1:1}:{url:stations[i].url,aac:false});
 assert.equal(routes.radio_info.text,1);
}
routes.radio_selected=-1;routes.url='https://example.test/custom.mp3';routes.start(null);
assert.deepEqual(routes.sent,{url:routes.url,aac:false},'custom URL never becomes an arbitrary relay input');
routes.accept=false;routes.start(null);assert.equal(routes.radio_info.text,0);
console.log('PASS: adaptive bitrate reserve, PCM hysteresis/EOF, two-ring jitter model, long-outage resume, partial-send integrity, cancellation, 20 presets including KR 6 / JP 7 and classical.');

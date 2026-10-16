// Source policy execution with mocks, without microphone access or API calls.
const fs=require('fs'),path=require('path'),vm=require('vm'),assert=require('assert');
const root=path.resolve(__dirname,'..'),read=p=>fs.readFileSync(path.join(root,p),'utf8');
const source=read('main/m3_voice.c'),audio=read('main/m3_audio.c'),ui=read('main/m3_ui.c');
function body(name,text=source){
 const m=new RegExp('\\b'+name+'\\([^)]*\\)\\s*\\{').exec(text);assert(m,name);
 let start=m.index+m[0].length,end=start,depth=1;
 while(depth){if(text[end]==='{')depth++;if(text[end]==='}')depth--;end++;}
 return text.slice(start,end-1);
}
const wav={VOICE_RATE:16000,memset:(p,v,n)=>p.fill(v,0,n),memcpy:(p,s,n)=>Buffer.from(s).copy(p,0,0,n)};
vm.createContext(wav);
for(const [name,args] of [['le16','p,value'],['le32','p,value'],['wav_header','p,bytes']])
 vm.runInContext('function '+name+'('+args+'){'+body(name).replace(/p \+ (\d+)/g,'p.subarray($1)')+'}',wav);
const header=Buffer.alloc(44);wav.wav_header(header,256000);
assert.equal(header.toString('ascii',0,4),'RIFF');assert.equal(header.readUInt32LE(4),256036);
assert.equal(header.toString('ascii',8,16),'WAVEfmt ');assert.equal(header.readUInt16LE(20),1);
assert.equal(header.readUInt16LE(22),1);assert.equal(header.readUInt32LE(24),16000);
assert.equal(header.readUInt32LE(28),32000);assert.equal(header.readUInt16LE(34),16);
assert.equal(header.readUInt32LE(40),256000);
function literal(name){
 const m=new RegExp('static const char '+name+'\\[\\] =([\\s\\S]*?);').exec(source);assert(m,name);
 // Match to the terminating C declaration, allowing semicolons inside MIME strings.
 const start=source.indexOf('static const char '+name+'[] =');
 const rest=source.slice(start).split(/;\r?\n/)[0];
 return [...rest.matchAll(/"(?:\\.|[^"\\])*"/g)].map(m=>JSON.parse(m[0])).join('');
}
const prefix=literal('prefix'),suffix=literal('suffix');
assert(prefix.includes('name="model"\r\n\r\ngpt-4o-mini-transcribe\r\n'));
assert(prefix.includes('name="language"\r\n\r\nko\r\n'));
assert(prefix.includes('name="response_format"\r\n\r\njson\r\n'));
assert(prefix.endsWith('filename="voice.wav"\r\nContent-Type: audio/wav\r\n\r\n'));
assert.equal(suffix,'\r\n--m3_voice_upload--\r\n');
const upload=Buffer.concat([Buffer.from(prefix),header,Buffer.alloc(256000),Buffer.from(suffix)]);
assert.equal(upload.length,Buffer.byteLength(prefix)+256044+Buffer.byteLength(suffix));
function policy(text){return text
 .replace(/port(?:ENTER|EXIT)_CRITICAL\(&lock\);/g,'')
 .replace(/\(unsigned char\)key\[i\]/g,'key.charCodeAt(i)')
 .replace(/\b(?:size_t|unsigned|bool) (\w+)/g,'let $1')
 .replace('state.text[0] = 0','state.text = ""')
 .replaceAll('sizeof(api_key)','256').replaceAll('strlen(key)','key.length')
 .replace('wipe(api_key, 256)','api_key = ""').replace('memcpy(api_key, key, length)','api_key = key')
 .replace('atomic_store(&finish_recording, false)','finish_recording = false')
 .replace('atomic_store(&finish_recording, true)','finish_recording = true')
 .replace('wipe(state.text, sizeof(state.text))','state.text = ""')
 .replace(/snprintf\(state.message, sizeof\(state.message\), "([^"]*)"\)/g,'state.message = "$1"');}
const state={api_key:'',finish_recording:false,state:{key_ready:false,busy:false,recording:false,job:0,revision:0,text:'old'},
 network_get_mode:()=>state.mode,mode:0,NET_MODE_STA_CONNECTED:1};
vm.createContext(state);
for(const [name,args] of [['m3_voice_set_key','key'],['m3_voice_prepare',''],['m3_voice_cancel','']])
 vm.runInContext('function '+name+'('+args+'){'+policy(body(name))+'}',state);
assert(!state.m3_voice_set_key('short'));
assert(!state.m3_voice_set_key('x'.repeat(256)));
assert(!state.m3_voice_set_key('x'.repeat(24)+'\r\nheader'));
assert(state.m3_voice_set_key('sk-'+'x'.repeat(40)));
assert.equal(state.m3_voice_prepare(),0,'offline capture is rejected');
state.mode=1;const job=state.m3_voice_prepare();assert(job>0&&state.state.busy);
assert.equal(state.m3_voice_prepare(),0,'only one pending voice job');
assert(!state.m3_voice_set_key('sk-'+'y'.repeat(40)),'cannot change key during upload');
state.m3_voice_cancel();assert(!state.state.busy);assert.equal(state.state.text,'');assert(state.state.job!==job);
assert(state.m3_voice_set_key(''));assert(!state.state.key_ready);assert.equal(state.api_key,'');
assert.equal(state.m3_voice_prepare(),0,'cleared key cannot send audio');
// Run capture slot selection on the actual C body.
const capture={samples:160,input:new Int16Array(320),cancelled:()=>false,
 codec:{},codec_result:(_,r)=>r===0,esp_codec_dev_read:(_,out)=>{out.set(capture.input);return 0;}};
vm.createContext(capture);vm.runInContext('function read(mono,samples){'+body('capture_read',audio)
 .replace('int16_t stereo[320];','let stereo = new Int16Array(320);')
 .replace('unsigned energy[2] = {0};','let energy = [0,0];')
 .replace(/\b(?:unsigned|int) (\w+)/g,'let $1')+'}',capture);
for(const slot of [0,1]){
 capture.input.fill(0);for(let i=0;i<160;i++)capture.input[i*2+slot]=i%2?1000:-1000;
 const mono=new Int16Array(160);assert(capture.read(mono,160));assert.equal(mono[0],-1000);assert.equal(mono[1],1000);
}
assert(!capture.read(new Int16Array(161),161));
assert(audio.includes('.din = M3_I2S_DIN'));assert(audio.includes('ESP_CODEC_DEV_WORK_MODE_BOTH'));
assert(!audio.includes('esp_codec_dev_set_in_mute('),'pinned ES8311 has no microphone mute callback');
assert(source.includes('.disable_auto_redirect = true'));assert(source.includes('esp_crt_bundle_attach'));
assert(!/nvs_(?:set|open)/.test(source),'session key and recordings are not persisted');
assert(source.includes('wipe(authorization, sizeof(authorization))'));
assert(ui.includes('lv_textarea_set_password_mode(voice_key, true)'));
assert(ui.includes('lv_textarea_get_accepted_chars(voice_target)'));
assert(ui.includes('lv_textarea_get_max_length(voice_target)'));
assert(ui.includes('if (voice_target) { m3_audio_stop(); voice_target = NULL; }'));
// Execute the actual microphone meter: RMS -> dBFS, instant attack, slow release, 0..100 scale.
{
 const m=/static void meter_update\([^)]*\)\s*\{([\s\S]*?)\n\}/.exec(source);assert(m,'meter_update');
 const meter={out:{},sqrtf:Math.sqrt,log10f:Math.log10,lroundf:Math.round,
  atomic_store:(target,value)=>meter.out[target]=value};
 vm.createContext(meter);
 vm.runInContext('function update(block,count,smoothed){'+m[1]
  .replace('uint64_t energy = 0;','let energy = 0;').replace('unsigned i = 0','let i = 0')
  .replace('(uint64_t)((int32_t)block[i] * block[i])','block[i] * block[i]')
  .replace('float rms = sqrtf((float)energy / (float)count);','const rms = sqrtf(energy / count);')
  .replace('float db','let db').replace('float level','const level').replaceAll('*smoothed_db','smoothed.db')
  .replace(/(\d+\.\d+)f/g,'$1').replace('(unsigned)(level + 0.5)','Math.floor(level + 0.5)')
  .replace('(int)lroundf(smoothed.db)','lroundf(smoothed.db)')
  .replaceAll('&mic_level','"level"').replaceAll('&mic_dbfs','"dbfs"').replace(/0u/g,'0').replace(/100u/g,'100')+'}',meter);
 const tone=amp=>Array.from({length:160},(_,i)=>Math.round(amp*Math.sin(i*2*Math.PI/16)));
 const s={db:-90};
 meter.update(new Array(160).fill(0),160,s);assert.equal(meter.out.level,0);assert.equal(meter.out.dbfs,-90,'silence');
 meter.update(tone(23170),160,s);assert.equal(meter.out.dbfs,-6,'0.707 FS sine has RMS 0.5 FS = -6 dBFS');assert.equal(meter.out.level,100);
 meter.update(new Array(160).fill(0),160,s);assert(meter.out.level>=85&&meter.out.level<100,'release is gradual (one 10 ms block of silence)');
 for(let i=0;i<60;++i)meter.update(new Array(160).fill(0),160,s);assert.equal(meter.out.level,0,'falls back to zero');
 const quiet={db:-90};meter.update(tone(1036),160,quiet);assert.equal(meter.out.dbfs,-33);assert.equal(meter.out.level,54,'-33 dBFS sits mid-scale');
 assert(source.includes('meter_update(block, 160, &meter_db);'));
 assert((source.match(/meter_reset\(\);/g)||[]).length>=3,'meter resets at start, after recording and on exit');
 assert(ui.includes('lv_timer_create(voice_meter_tick, 40, NULL)'));
 assert(/unsigned level = m3_voice_level\(\);/.test(ui),'UI polls the lock-free level, not the whole voice state');
}
// The UI task runs every callback; 8 KiB overflowed during voice input.
assert(read('main/m3_board.c').includes('port.task_stack = 16384;'));
assert(!/^\s+m3_agent_state_t agent;/m.test(ui),'the 1.9 KiB agent state copy must not live on the UI stack');
assert(/static char text\[768\];/.test(ui));
console.log('PASS: WAV/multipart format, session key validation/clear, offline/busy admission, cancel generation, mono slot selection, numeric/length review and privacy contracts.');

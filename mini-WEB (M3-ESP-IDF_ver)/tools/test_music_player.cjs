// Execute local-file playback and gain C bodies with file/codec mocks. No build.
const fs=require('fs'),path=require('path'),vm=require('vm'),assert=require('assert');
const root=path.resolve(__dirname,'..'),read=p=>fs.readFileSync(path.join(root,p),'utf8').replace(/\r\n/g,'\n');
const music=read('main/m3_music.c'),audio=read('main/m3_audio.c');
function body(name,source=music){const m=new RegExp('\\b'+name+'\\([^)]*\\)\\s*\\{').exec(source);assert(m,name);
 let start=m.index+m[0].length,end=start,depth=1;while(depth){if(source[end]==='{')depth++;if(source[end]==='}')depth--;end++;}return source.slice(start,end-1);}
const gain=body('m3_pcm_gain',read('main/m3_pcm.c')).replace(/\bint32_t (\w+)/g,'let $1')
 .replace(/\(int32_t\)|\(int16_t\)/g,'').replace('sample * percent / 100','Math.trunc(sample * percent / 100)');
const dsp={};vm.createContext(dsp);vm.runInContext('function gain(sample,percent){'+gain+'}',dsp);
for(let percent=100;percent<=150;percent++)for(let sample=-32768;sample<=32767;sample+=17)
 assert.equal(dsp.gain(sample,percent),Math.min(32767,Math.max(-32768,Math.trunc(sample*percent/100))));
assert.equal(dsp.gain(30000,150),32767);assert.equal(dsp.gain(-30000,150),-32768);assert.equal(dsp.gain(1000,150),1500);
assert.equal(dsp.gain(-1,101),-1);assert.equal(dsp.gain(1234,0),1234,'hardware owns attenuation 0..100');
const writer={volume:150,writes:[],boost_pcm:new Int16Array(960),codec:{},cancelled:()=>false,apply_volume:()=>true,
 m3_pcm_gain:dsp.gain,codec_result:(_,result)=>result===0,
 esp_codec_dev_write:(_,samples,bytes)=>{writer.writes.push(Array.from(samples.subarray(0,bytes/2)));return 0;}};
vm.createContext(writer);vm.runInContext('function write(samples,bytes){'+body('write_pcm',audio)
 .replace(/\bunsigned (\w+)/g,'let $1').replace('const int16_t *input','let input')
 .replace('atomic_load(&volume)','volume').replaceAll('sizeof(int16_t)','2').replaceAll('(int)','')
 .replace('input += count','input = input.subarray(count)')+'}',writer);
const raw=new Int16Array(2000);for(let i=0;i<raw.length;i++)raw[i]=i%2?-30000:30000;
const untouched=Array.from(raw);assert(writer.write(raw,raw.byteLength));assert.deepEqual(Array.from(raw),untouched,'boost never modifies the caller buffer');
assert.deepEqual(writer.writes.map(b=>b.length),[960,960,80]);assert.equal(writer.writes.flat()[0],32767);assert.equal(writer.writes.flat()[1],-32768);
writer.volume=75;writer.writes=[];assert(writer.write(raw,raw.byteLength));assert.deepEqual(writer.writes[0],untouched,'0..100 preserves PCM and uses codec attenuation');
writer.volume=150;writer.writes=[];writer.cancelled=()=>writer.writes.length>=1;assert(!writer.write(raw,raw.byteLength));assert.equal(writer.writes.length,1);
const pausing={paused:true,ticks:0,drains:0,pdMS_TO_TICKS:n=>n,vTaskDelay:()=>{if(++pausing.ticks===3)pausing.paused=false;}};
vm.createContext(pausing);vm.runInContext('function wait(out){'+body('wait_playing')
 .replaceAll('atomic_load(&paused)','paused').replaceAll('out->','out.')+'}',pausing);
assert(pausing.wait({drain:()=>{pausing.drains++;return true;},cancelled:()=>false}));assert.equal(pausing.drains,1);assert.equal(pausing.ticks,3);
pausing.paused=true;pausing.ticks=0;assert(!pausing.wait({drain:()=>false,cancelled:()=>false}));assert.equal(pausing.ticks,0);
const validator={SD_MEDIA_ROOT:'/sdcard',strlen:s=>s.length,strstr:(s,x)=>s.includes(x),strncmp:(a,b,n)=>a.slice(0,n)===b.slice(0,n)?0:1,
 m3_utf8_valid:()=>true,
 supported:s=>/\.(wav|mp3|aac)$/i.test(s)};
vm.createContext(validator);vm.runInContext('function valid(path){'+body('m3_music_path_valid')
 .replace('sizeof(catalogs[0][0].path)','384').replace('SD_MEDIA_ROOT "/media/"','"/sdcard/media/"')
 .replaceAll('strlen(SD_MEDIA_ROOT "/media/")','strlen("/sdcard/media/")')
 .replace('for (const char *p = path; *p; ++p)','for (let i=0;i<path.length;i++)')
 .replace('(unsigned char)*p','path.charCodeAt(i)').replaceAll('*p','path[i]')+'}',validator);
for(const p of ['/sdcard/media/한국어.mp3','/sdcard/media/album/song.wav','/spiffs/media/song.aac','/spiffs/song.MP3'])assert(validator.valid(p),p);
for(const p of ['/etc/test.wav','/sdcard/song.mp3','/spiffs/../secret.mp3','/spiffs/index.html','/spiffs/a\n.mp3','/spiffs/a\\b.mp3','/spiffs//song.mp3'])assert(!validator.valid(p),p);

const playback=body('m3_music_run')
 .replace(/\bFILE \*file/g,'var file').replace(/\buint8_t \*/g,'var ').replace(/\bint16_t \*stereo/g,'var stereo')
 .replace(/, \*(\w+)/g,', $1')
 .replace(/const char \*(\w+)/g,'var $1')
 .replace(/\b(?:esp_audio_simple_dec_handle_t|bool|size_t|uint64_t|unsigned|long|esp_audio_err_t) (\w+)/g,'var $1')
 .replace('m3_wav_info_t wave;','var wave = {};').replace('char next_path[384] = {0};','var next_path = Buffer.alloc(384);')
 .replace(/esp_aac_dec_cfg_t aac_cfg = \{([\s\S]*?)\};/,'var aac_cfg = {$1};')
 .replace(/esp_audio_simple_dec_cfg_t config = \{([\s\S]*?)\};/,'var config = {$1};')
 .replace(/esp_audio_simple_dec_raw_t raw = \{([\s\S]*?)\};/,'var raw = {$1};')
 .replace(/esp_audio_simple_dec_out_t frame = \{([\s\S]*?)\};/,'var frame = {$1};')
 .replace(/esp_audio_simple_dec_info_t info = \{0\};/,'var info = {};')
 .replace(/\.(aac_plus_enable|dec_type|dec_cfg|cfg_size|buffer|len|eos) =/g,'$1:')
 .replace('void *samples','var samples').replaceAll('out->','out.').replaceAll('SD_MEDIA_ROOT "/"','"/sdcard/"')
 .replaceAll('NULL','null').replaceAll('sizeof(aac_cfg)','4').replaceAll('sizeof(next_path)','384')
 .replace(/snprintf\(state.title, sizeof\(state.title\), "%s", ([^;]+)\);/g,'state.title = $1;')
 .replace(/port(?:ENTER|EXIT)_CRITICAL\(&lock\);/g,'')
 .replaceAll('&wave','wave').replaceAll('&info','info').replaceAll('&config','config').replaceAll('&aac_cfg','aac_cfg')
 .replaceAll('&decoder','decoderRef').replaceAll('&raw, &frame','raw, frame')
 .replace('cache + loaded','cache.subarray(loaded)').replace('cache + position','cache.subarray(position)').replaceAll('input + offset','input.subarray(offset)')
 .replace('(char *)','').replace('(uint64_t)','')
 .replace('stereo[i * 2] = stereo[i * 2 + 1] = ((int16_t *)pcm)[i]','copy_stereo(stereo,i,pcm.readInt16LE(i*2))')
 .replace('decoder, raw, frame','decoderRef.value, raw, frame')
 .replace('decoder, info','decoderRef.value, info').replace('if (decoder) esp_audio_simple_dec_close(decoder)','if (decoderRef.value) esp_audio_simple_dec_close(decoderRef.value)')
 .replaceAll('goto done;','break run;').replace('\ndone:','\n}\n')
 ;
const script='function play(path,out){run:{'+playback+'return {ok,frames,rate};}';
function wave(channels=2){const frames=1700,b=Buffer.alloc(44+frames*channels*2);
 b.write('RIFF');b.writeUInt32LE(b.length-8,4);b.write('WAVEfmt ',8);b.writeUInt32LE(16,16);b.writeUInt16LE(1,20);
 b.writeUInt16LE(channels,22);b.writeUInt32LE(24000,24);b.writeUInt32LE(24000*channels*2,28);b.writeUInt16LE(channels*2,32);b.writeUInt16LE(16,34);
 b.write('data',36);b.writeUInt32LE(b.length-44,40);for(let i=0;i<frames*channels;i++)b.writeInt16LE(i%2? -12000:23000,44+i*2);return b;}
function run(data,{name='/sdcard/media/song.wav',failAllocation=0,cancelAtRead=Infinity,failWrite=false,failRead=false}={}){
 const ctx={MUSIC_FILE_LIMIT:256*1024*1024,MUSIC_PREFETCH_LIMIT:8*1024*1024,MUSIC_READ_BYTES:4096,MUSIC_PCM_BYTES:32768,
 MALLOC_CAP_SPIRAM:4,MALLOC_CAP_8BIT:2,ESP_AUDIO_SIMPLE_DEC_TYPE_AAC:1,ESP_AUDIO_SIMPLE_DEC_TYPE_MP3:2,ESP_AUDIO_ERR_OK:0,
 state:{repeat:false,selected:0,count:1,title:""},catalog_set:0,catalogs:[[{path:name,name:"Mock song"}]],decoderRef:{value:null},allocations:[],freed:[],writes:[],statuses:[],reads:0,position:0,closed:0,unlocks:0,drains:0,begins:[],encoded:[],
 Buffer,strcmp:(a,b)=>a===b?0:1,snprintf:(out,n,format,text)=>{out.fill(0);out.write(text,0,n-1);},m3_music_path_valid:validator.valid,strlen:s=>s.length,strncmp:validator.strncmp,strcasecmp:(a,b)=>a.toLowerCase()===b.toLowerCase()?0:1,
 extension:s=>path.extname(s),status:(...args)=>ctx.statuses.push(args),sd_media_admin_lock:()=>true,sd_media_admin_unlock:()=>ctx.unlocks++,
 fopen:()=>({}),SEEK_END:2,SEEK_SET:0,ftell:()=>data.length,
 fseek:(_,offset,origin)=>{ctx.position=origin===2?data.length+offset:offset;return 0;},setvbuf:()=>0,_IOFBF:1,
 fread:(dest,_,n)=>{ctx.reads++;if(failRead)return 0;const bytes=Math.min(n,data.length-ctx.position);data.copy(dest,0,ctx.position,ctx.position+bytes);ctx.position+=bytes;return bytes;},
 fclose:()=>ctx.closed++,free:p=>{if(p)ctx.freed.push(p);},
 heap_caps_malloc:(n,caps)=>{assert.equal(caps,6);if(ctx.allocations.length+1===failAllocation){ctx.allocations.push(null);return null;}const b=Buffer.alloc(n);ctx.allocations.push(b);return b;},
 memcpy:(dest,src,n)=>src.copy(dest,0,0,n),copy_stereo:(b,i,v)=>{b.writeInt16LE(v,i*4);b.writeInt16LE(v,i*4+2);},
 m3_wav_parse_limit:(_,info,limit)=>{assert.equal(limit,ctx.MUSIC_FILE_LIMIT);if(data.toString('ascii',0,4)!=='RIFF')return false;
  Object.assign(info,{channels:data.readUInt16LE(22),sample_rate:data.readUInt32LE(24),data_offset:44,data_bytes:data.length-44});ctx.position=44;return true;},
 m3_audio_decoders_ready:()=>true,esp_audio_simple_dec_open:(_,ref)=>{ref.value={};return 0;},
 esp_audio_simple_dec_process:(_,raw,frame)=>{const n=Math.min(111,raw.len);ctx.encoded.push(...raw.buffer.subarray(0,n));raw.consumed=n;
  frame.decoded_size=8;frame.buffer.writeInt16LE(1,0);frame.buffer.writeInt16LE(-1,2);frame.buffer.writeInt16LE(2,4);frame.buffer.writeInt16LE(-2,6);return 0;},
 esp_audio_simple_dec_get_info:(_,info)=>{Object.assign(info,{sample_rate:44100,channel:2,bits_per_sample:16});return 0;},
 esp_audio_simple_dec_close:()=>{},m3_music_next:()=>{throw Error('unexpected auto advance');},
 wait_playing:out=>!out.cancelled()};
 const out={cancelled:()=>ctx.reads>=cancelAtRead,begin:rate=>{ctx.begins.push(rate);return true;},
 write:(samples,n)=>{if(failWrite)return false;
  if(failAllocation!==5&&name.startsWith('/sdcard/'))assert.equal(ctx.unlocks,1,'PSRAM cached playback releases SD before PCM output');
  ctx.writes.push(Buffer.from(samples.subarray(0,n)));return true;},
 message:()=>{},drain:()=>{ctx.drains++;return true;}};
 vm.createContext(ctx);vm.runInContext(script,ctx);ctx.result=ctx.play(name,out);return ctx;
}
for(const channels of [1,2]){
 const data=wave(channels),r=run(data);assert(r.result.ok);assert.equal(r.closed,1);assert.equal(r.unlocks,1);assert.equal(r.drains,1);
 const pcm=Buffer.concat(r.writes);assert.equal(pcm.length,1700*4);
 for(let i=0;i<1700;i++)for(let ch=0;ch<2;ch++)assert.equal(pcm.readInt16LE(i*4+ch*2),data.readInt16LE(44+(i*channels+(channels===1?0:ch))*2));
 assert.equal(r.result.frames,1700);assert.equal(r.freed.length,r.allocations.filter(Boolean).length);
 const fallback=run(data,{failAllocation:5});assert(fallback.result.ok);assert.deepEqual(Buffer.concat(fallback.writes),pcm,'cache failure falls back to buffered file reads');
}
const compressed=Buffer.from(Array.from({length:12345},(_,i)=>i%251));
for(const ext of ['mp3','aac']){const r=run(compressed,{name:'/spiffs/track.'+ext});assert(r.result.ok);assert.equal(r.unlocks,0);assert.deepEqual(Buffer.from(r.encoded),compressed);assert(r.writes.length>0);}
for(const opts of [{failAllocation:3},{cancelAtRead:1},{failRead:true},{failWrite:true}]){
 const r=run(wave(),opts);assert(!r.result.ok);assert.equal(r.closed,1);assert.equal(r.unlocks,1);
 assert.equal(r.freed.length,r.allocations.filter(Boolean).length,'all allocated working buffers released');
}
assert(music.includes('M3_PSRAM_BSS track_t catalogs[2]'));assert(!music.includes('xTaskCreate'));
assert(audio.includes('requested > 100 ? 100 : requested'));assert(audio.includes('m3_pcm_gain(input[i], gain)'));
assert(audio.includes('percent > 150 ? 150 : percent'));assert(audio.includes('m3_music_run(request.name, &output)'));
const follow={requests:[],active_epoch:7,PLAY_MUSIC:9,pdTRUE:true,cancelled:()=>false,
 m3_music_path_valid:validator.valid,xQueueSend:(queue,request)=>{if(queue.length)return false;queue.push(request);return true;}};
vm.createContext(follow);vm.runInContext('function follow(path){'+body('m3_audio_music_follow',audio)
 .replace('request_t request = {.kind = PLAY_MUSIC, .epoch = active_epoch};','let request = {kind:PLAY_MUSIC,epoch:active_epoch};')
 .replace('snprintf(request.name, sizeof(request.name), "%s", path);','request.name = path;')
 .replace('&request','request')+'}',follow);
assert(follow.follow('/spiffs/next.mp3'));assert.equal(follow.requests[0].epoch,7);
assert(!follow.follow('/spiffs/another.mp3'),'auto advance does not overwrite a queued request');
follow.requests=[];follow.cancelled=()=>true;assert(!follow.follow('/spiffs/next.mp3'));
assert(!body('m3_audio_music_follow',audio).includes('m3_audio_stop()'),'auto advance must not cancel a newer user action');
const registry={initialized:false,ESP_AUDIO_ERR_OK:0,low:0,simple:0,
 esp_audio_dec_register_default:()=>{registry.low++;return 0;},esp_audio_simple_dec_register_default:()=>{registry.simple++;return 0;}};
vm.createContext(registry);vm.runInContext('function ready(){'+body('m3_audio_decoders_ready',audio).replace('static bool initialized;','')+'}',registry);
assert(registry.ready());assert(registry.ready());assert.equal(registry.low,1);assert.equal(registry.simple,1);
assert(music.includes('m3_audio_decoders_ready()')&&read('main/m3_radio.c').includes('m3_audio_decoders_ready()'));
assert(read('main/m3_runtime.c').includes('m3_music_poll()'));assert(read('main/m3_ui.c').includes('APP_TUBE, APP_MUSIC, APP_COUNT'));
console.log('PASS: saturating 100..150 gain, storage path isolation, actual local-player WAV/MP3/AAC loop with mocked decoders, mono/stereo bytes, prefetch and fallback, cancel/read/write/OOM cleanup, sole codec ownership and service/UI integration.');

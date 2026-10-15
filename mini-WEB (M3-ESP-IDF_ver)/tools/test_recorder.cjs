// Execute recorder C control flow with microphone/filesystem mocks. No target compiler or SD hardware.
const fs=require('fs'),path=require('path'),vm=require('vm'),assert=require('node:assert/strict');
const root=path.resolve(__dirname,'..'),read=p=>fs.readFileSync(path.join(root,p),'utf8');
const source=read('main/m3_recorder.c');
function body(name,text=source){const m=new RegExp('\\b'+name+'\\([^)]*\\)\\s*\\{').exec(text);assert(m,name);
 let i=m.index+m[0].length,start=i,depth=1;while(depth){if(text[i]==='{')depth++;if(text[i]==='}')depth--;i++;}return text.slice(start,i-1);}
function js(s){return s.replace(/\/\*[\s\S]*?\*\//g,'').replace(/\((?:uint8_t|unsigned long long|unsigned long|long)\)/g,'')
 .replace(/port(?:ENTER|EXIT)_CRITICAL\(&lock\);/g,'')
 .replace(/atomic_store\(&finish_requested, (true|false)\)/g,'finish_requested = $1')
 .replaceAll('atomic_load(&finish_requested)','finish_requested')
 .replace(/\b(?:const char \*|FILE \*|int16_t \*|char \*)(\w+)/g,'let $1')
 .replace(/\b(?:unsigned|uint32_t|int|bool|time_t) (\w+)/g,'let $1')
 .replace(/\((?:uint8_t|unsigned long long|unsigned long|long)\)/g,'')
 .replaceAll('sizeof(int16_t)','2').replace(/sizeof\([^)]*\)/g,'0')
 .replaceAll('->','.').replace(/\b(\d+)U\b/g,'$1');}
let run=body('m3_recorder_run')
 .replace('char filename[64] = {0}, final_path[160] = {0};',
 'let filename={value:""},final_path={value:""};')
 .replace('char temporary[sizeof(final_path) + sizeof(".part") - 1] = {0};',
 'let temporary={value:""};')
 .replace('struct tm date;','let date={};').replace('struct stat existing;','let existing={};')
 .replace('char stamp[32];','let stamp={value:""};')
 .replace('SD_MEDIA_ROOT M3_RECORDER_FOLDER "/%s"','"/sdcard/recordings/%s"')
 .replaceAll('SD_MEDIA_ROOT M3_RECORDER_FOLDER','"/sdcard/recordings"')
 .replaceAll('&date','date').replaceAll('&now','now').replaceAll('&existing','existing')
 .replace('    if (atomic_load(&finish_requested) || capture->cancelled()) goto done;',
 '    recording: { if (atomic_load(&finish_requested) || capture->cancelled()) goto done;')
 .replaceAll('goto done;','break recording;').replace('done:','}');
run=js(run);
const header=js(body('m3_recorder_wav_header')).replace(/out \+ (\d+)/g,'out.subarray($1)');
const checkpoint=js(body('checkpoint')).replace('uint8_t header[44];','let header = new Uint8Array(44);');
function setup(options={}){
 const unwrap=x=>x&&typeof x==='object'&&'value'in x?x.value:x;
 const c={NULL:null,O_WRONLY:1,O_CREAT:2,O_EXCL:4,EEXIST:17,errno:0,_IOFBF:0,SEEK_SET:0,SEEK_END:2,
 M3_RECORDER_RATE:16000,M3_RECORDER_MAX_SECONDS:1,MALLOC_CAP_SPIRAM:1,MALLOC_CAP_8BIT:2,
 ESP_LOGE:()=>{},state:{error_stage:{value:""},error_code:0,busy:false,recording:false,saved:false,job:0,revision:0,message:{value:''},filename:{value:''}},
 running:false,finish_requested:false,events:[],files:new Map(),fd:0,blocks:0,writes:0,allocs:0,freed:0,
 strcmp:(a,b)=>unwrap(a)===unwrap(b)?0:1,
 memset:(out,v,n)=>out.fill(v,0,n),memcpy:(out,text,n)=>out.set(Buffer.from(text).subarray(0,n)),
 snprintf:(out,_size,format,...args)=>{let i=0;out.value=format.replace(/%(?:0\d+)?(?:llu|lu|lx|s|u)/g,m=>{
 const v=unwrap(args[i++]);return m.endsWith('lx')?Number(v).toString(16).padStart(8,'0'):String(v);});},
 time:()=>0,localtime_r:()=>false,strftime:()=>{},esp_timer_get_time:()=>18000000,esp_random:()=>42,
 sd_media_admin_lock:()=>{c.events.push('lock');return !options.noSD;},
 sd_media_admin_unlock:()=>c.events.push('unlock'),sd_media_changed:()=>c.events.push('changed'),
 mkdir:()=>options.mkdirFail?-1:0,stat:()=>-1,
 heap_caps_malloc:size=>{c.allocs++;return options.noMemory?null:size===320?new Int16Array(160):Buffer.alloc(size);},
 free:buffer=>{if(buffer)c.freed++;},
 open:(p,flags)=>{assert(flags&4,'files must be created exclusively');
 if(options.collision && c.fd===0){c.fd++;c.errno=17;return -1;}
 if(options.createFail)return -1;const f={path:unwrap(p),bytes:Buffer.alloc(0),pos:0};c.files.set(f.path,f);c.current=f;return ++c.fd;},
 fdopen:()=>options.fdopenFail?null:c.current,close:()=>c.events.push('close-fd'),setvbuf:()=>0,
 fwrite:(data,_size,count,f)=>{c.writes++;if(options.writeFail===c.writes)return 0;
 const b=typeof data==='string'?Buffer.from(data):Buffer.from(data.buffer,data.byteOffset,data.byteLength);
 if(f.pos+count>f.bytes.length){const grown=Buffer.alloc(f.pos+count);f.bytes.copy(grown);f.bytes=grown;}
 b.copy(f.bytes,f.pos,0,count);f.pos+=count;return count;},
 fseek:(f,p,how)=>{c.seeks=(c.seeks||0)+1;if(options.seekFail===c.seeks){c.errno=5;return -1;}f.pos=how===2?(options.staleEnd?44:f.bytes.length):p;return 0;},fflush:()=>options.flushFail?-1:0,
 fileno:()=>1,fsync:()=>{c.events.push('sync');return options.syncFail?-1:0;},
 fclose:()=>{c.events.push('close');return options.closeFail?-1:0;},
 rename:(a,b)=>{c.events.push('rename');if(options.renameFail)return -1;
 const f=c.files.get(unwrap(a));c.files.delete(unwrap(a));f.path=unwrap(b);c.files.set(f.path,f);return 0;},
 unlink:p=>{c.files.delete(unwrap(p));c.events.push('unlink');}};
 vm.createContext(c);
 const definitions='function put32(out,value){'+js(body('put32'))+'}\n'+
 'function m3_recorder_wav_header(out,bytes){'+header+'}\n'+
 'function record_error(stage){'+js(body('record_error'))+'}\n'+
 'function checkpoint(file,bytes){'+checkpoint+'}\n'+
 'function progress(bytes,samples,count){'+js(body('progress'))+'}\n'+
 'function m3_recorder_prepare(){'+js(body('m3_recorder_prepare'))+'}\n'+
 'function m3_recorder_cancel(){'+js(body('m3_recorder_cancel'))+'}\n'+
 'function m3_recorder_finish(){'+js(body('m3_recorder_finish'))+'}\n'+
 'function run(job,capture){'+run+'}';
 vm.runInContext(definitions,c);
 c.capture={begin:()=>{c.events.push('begin');return !options.beginFail;},
 end:()=>c.events.push('end'),cancelled:()=>!!c.cancelled,
 read:pcm=>{if(options.readFail && c.blocks===2)return false;
 pcm.fill(16000);c.blocks++;
 if(!options.limit && c.blocks===3){if(options.cancelDuring){c.cancelled=true;c.m3_recorder_cancel();}else c.m3_recorder_finish();}
 return true;}};
 return c;
}
function record(options){const c=setup(options),job=c.m3_recorder_prepare();assert(job);c.run(job,c.capture);
 assert(!c.state.busy&&!c.state.recording);assert.equal(c.freed,options?.noMemory?0:c.allocs);return c;}
let c=record();assert(c.state.saved);assert.equal(c.state.bytes,960);assert.equal(c.state.message.value,'Recording saved');
const wav=[...c.files.values()][0].bytes;
assert.equal(wav.toString('ascii',0,4),'RIFF');assert.equal(wav.toString('ascii',8,16),'WAVEfmt ');
assert.equal(wav.readUInt32LE(4),wav.length-8);assert.equal(wav.readUInt16LE(20),1);
assert.equal(wav.readUInt16LE(22),1);assert.equal(wav.readUInt32LE(24),16000);
assert.equal(wav.readUInt32LE(28),32000);assert.equal(wav.readUInt16LE(32),2);assert.equal(wav.readUInt16LE(34),16);
assert.equal(wav.readUInt32LE(40),960);assert.equal(wav.readInt16LE(44),16000);
assert(c.events.indexOf('end')<c.events.indexOf('close'));assert(c.events.indexOf('close')<c.events.indexOf('unlock'));
assert.equal(c.state.level,0);assert.equal(c.events.at(-1),'unlock');
c=record({limit:true});assert.equal(c.blocks,100);assert.equal(c.state.bytes,32000);assert(c.state.saved);
c=record({cancelDuring:true});assert(c.state.saved,'global audio stop saves captured PCM');
c=record({collision:true});assert(c.state.saved,'name collisions retry without overwriting');
for(const failure of ['noSD','mkdirFail','noMemory','createFail','fdopenFail','beginFail','flushFail']){
 c=record({[failure]:true});assert(!c.state.saved,failure);assert.equal(c.files.size,0,failure);
 if(failure==='beginFail')assert(c.events.includes('end'),'partial microphone open is closed');
}
for(const failure of [{writeFail:3},{syncFail:true},{closeFail:true},{renameFail:true}]){
 c=record(failure);assert(!c.state.saved);assert([...c.files.keys()].every(p=>p.endsWith('.part')));
 assert.equal(c.state.message.value,'Recording incomplete; partial file retained');
}
c=record({readFail:true});assert(c.state.saved);assert.equal(c.state.bytes,640);assert.equal(c.state.message.value,'Microphone read failed');
c=setup();let old=c.m3_recorder_prepare();assert.equal(c.m3_recorder_prepare(),0);
c.m3_recorder_cancel();assert(!c.state.busy);const fresh=c.m3_recorder_prepare();assert.notEqual(old,fresh);
c.run(old,c.capture);assert(c.state.busy);assert.equal(c.events.length,0,'stale jobs cannot open a microphone or clear a new job');
c.m3_recorder_finish();c.run(fresh,c.capture);assert(!c.state.busy);assert.equal(c.files.size,0);
const audio=read('main/m3_audio.c'),ui=read('main/m3_ui.c');
assert(!audio.includes('PLAY_TEST'));assert(!ui.includes('APP_AUDIO'));assert(!ui.includes('test_audio('));
assert(audio.includes('m3_recorder_run(request.speed, &capture)'));
assert(audio.indexOf('m3_recorder_cancel();')<audio.indexOf('xQueueReset(requests)'));
assert(read('main/CMakeLists.txt').includes('"m3_recorder.c"'));
let screen=body('update_recorder',ui).replace('m3_recorder_state_t recorded;','let recorded={};')
 .replace('&recorded','recorded').replace('char text[160];','let text={value:""};')
 .replace('bool enabled[] = {','let enabled = [').replace('snapshot.sd && !recorded.busy};','snapshot.sd && !recorded.busy];');
const view={snapshot:{sd:true},record:{error_stage:'',error_code:0,busy:false,recording:false,saved:false,seconds:0,level:0,message:'',filename:''},
 recorder_info:{},recorder_time:{},recorder_path:{},recorder_meter:{},recorder_actions:Array.from({length:4},()=>({label:{}})),
 T_AUDIO_UNAVAILABLE:'unavailable',T_AUDIO_STOP:'stop',T_REC_PLAY:'play',T_REC_HINT:'ready',
 LV_SYMBOL_STOP:'stop-icon',LV_SYMBOL_PLAY:'play-icon',LV_ANIM_OFF:0,LV_STATE_DISABLED:1,M3_RECORDER_FOLDER:'/recordings',
 m3_recorder_state:out=>Object.assign(out,view.record),tr:x=>x,status_text:x=>x,
 set_text:(out,text)=>out.text=text?.value??text,
 snprintf:(out,_size,format,...args)=>{let i=0;out.value=format.replace(/%(?:\.\d+)?(?:02u|u|s|d)/g,m=>{
 const value=args[i++];return m==='%02u'?String(Math.floor(value)).padStart(2,'0'):String(value);});},
 lv_obj_get_child:obj=>obj.label,lv_bar_set_value:(out,value)=>out.value=value,
 lv_obj_add_state:obj=>obj.disabled=true,lv_obj_remove_state:obj=>obj.disabled=false};
vm.createContext(view);vm.runInContext('function update(audio){'+js(screen)+'}',view);
const audioState={ready:true,playing:false,volume:0,message:'playing'};
view.update(audioState);assert(!view.recorder_actions[0].disabled,'microphone works with speaker volume 0');
view.snapshot.sd=false;view.update(audioState);assert(view.recorder_actions[0].disabled);assert(view.recorder_actions[3].disabled);
assert.equal(view.recorder_info.text,'SD unavailable or busy');
view.snapshot.sd=true;view.record.busy=view.record.recording=true;view.record.level=65;
view.update({...audioState,playing:true});assert(view.recorder_actions[0].disabled);assert(!view.recorder_actions[1].disabled);
assert(view.recorder_actions[2].disabled);assert(view.recorder_actions[3].disabled);assert.equal(view.recorder_meter.value,65);
view.record.busy=view.record.recording=false;view.record.saved=true;view.record.filename='rec_last.wav';
view.update(audioState);assert(!view.recorder_actions[2].disabled);assert(view.recorder_path.text.includes('/recordings/\nrec_last.wav'));
view.update({...audioState,playing:true});assert.equal(view.recorder_actions[2].label.text,'stop-icon stop');
c=record({staleEnd:true});assert(c.state.saved);assert.equal([...c.files.values()][0].bytes.length,1004,'checkpoint uses the exact PCM offset rather than cached SEEK_END');
c=record({seekFail:3});assert(!c.state.saved);assert.equal(c.state.error_stage.value,'header-seek');assert.equal(c.state.error_code,5);
view.record.error_stage='sync';view.record.error_code=5;view.record.message='Recording incomplete; partial file retained';view.update(audioState);assert(view.recorder_info.text.includes('sync (5)'),'save failure stage and errno are visible');
console.log('PASS: actual C recorder flow with PCM/WAV round-trip, stop/limit/cancel, exclusive names, SD/allocator/mic/write/flush/sync/close/rename failures, partial retention, cleanup and stale-job ownership. Host mocks only.');

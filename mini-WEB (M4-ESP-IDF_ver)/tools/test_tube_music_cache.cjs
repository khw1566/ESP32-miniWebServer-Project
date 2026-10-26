// Actual cache ownership/producer/playback/SD-save C bodies, with host SDK mocks. No compiler.
const fs=require('fs'),path=require('path'),vm=require('vm'),assert=require('assert/strict');
const root=path.resolve(__dirname,'..'),source=fs.readFileSync(path.join(root,'main/m4_tube_music.inc'),'utf8');
function body(name,text=source){const m=new RegExp('\\b'+name+'\\([^;]*?\\)\\s*\\{').exec(text);assert(m,name);let i=m.index+m[0].length,start=i,d=1;while(d){if(text[i]==='{')d++;if(text[i]==='}')d--;i++;}return text.slice(start,i-1);}
function translate(code){return code.replace(/\/\*[\s\S]*?\*\//g,'').replace('(void)unused;','').replaceAll('->','.')
 .replace(/\((?:unsigned|uint64_t|int64_t|size_t|char \*)\)/g,'')
 .replace(/\b(?:const )?(?:music_slot_t|uint8_t|char) \*/g,'var ')
 .replace(/\b(?:unsigned|bool|int64_t|uint64_t|size_t|int|FILE \*|esp_http_client_handle_t |esp_audio_simple_dec_handle_t )\s*/g,'var ')
 .replace(/\((?:unsigned|uint64_t|int64_t|size_t|char \*)\)/g,'').replace(/\b(\d+)LL\b/g,'$1')
 .replace(/sizeof\([^)]*\)/g,'192').replace(/\.(\w+)\s*=/g,(m,n)=>m) // Struct initializers below only.
 .replace(/=\s*\{([^{}]*)\}/g,(_m,v)=>'={'+(v.trim()==='0'?'':v.replace(/\.(\w+)\s*=/g,'$1:'))+'}')
 .replace(/&(?:music_lock|player_lock|service_lock)/g,'null').replaceAll('&music_slots','music_slots').replace('atomic_load(&paused)','paused')
 .replaceAll('music_save_id[0]=0','music_save_id=""').replaceAll('music_wanted[1][0]=0','music_wanted[1]=""')
 .replace(/&(?:raw|frame|info|config)\b/g,m=>m.slice(1))
 .replace(/snprintf\((error|\w+(?:\.\w+|\[\d\])?),\s*(?:192|error_size),\s*([^;]+)\);/g,(_m,target,args)=>`${target==='error'?'error.value':target}=format(${args});`)
 .replace(/memcpy\((\w+\.\w+),([^,]+),192\);/g,'$1=$2;')
 .replaceAll('slot.data+position','slot.data.subarray(position)').replaceAll('slot.data+bytes','slot.data.subarray(bytes)')
 .replaceAll('pcm+skip*4','pcm.subarray(skip*4)');}
const blank=()=>({id:'',data:null,state:0,refs:0,bytes:0,total:0,duration:2,message:'',title:'Song',channel:'Artist',channel_id:'',job:''});
const c={Buffer,NULL:null,MUSIC_EMPTY:0,MUSIC_LOADING:1,MUSIC_READY:2,MUSIC_FAILED:3,MUSIC_DECODE_BYTES:32768,
 music_slots:[blank(),blank()],music_wanted:['',''],music_save_id:'',music_save_title:'',music_lock:{},
 portMAX_DELAY:0,ESP_AUDIO_ERR_OK:0,ESP_AUDIO_SIMPLE_DEC_TYPE_MP3:1,MALLOC_CAP_SPIRAM:1,MALLOC_CAP_8BIT:2,
 xSemaphoreTake:()=>{},xSemaphoreGive:()=>{},portENTER_CRITICAL:()=>{},portEXIT_CRITICAL:()=>{},
 strcmp:(a,b)=>a===b?0:1,strlen:s=>s.length,video_id_valid:id=>typeof id==='string'&&/^[A-Za-z0-9_-]{11}$/.test(id),
 snprintf:()=>{throw Error('untranslated snprintf');},format:(fmt,...args)=>{let i=0;return fmt.replace(/%(?:\.(\d+))?([sud])/g,(_m,n,t)=>{const v=args[i++];return t==='s'?(n?String(v).slice(0,+n):String(v)):String(v);});},
 memset:(obj)=>Object.assign(obj,blank()),free:a=>{if(a)c.freed.push(a);},freed:[],m4_preferences_restarting:()=>false,
 download_stopped:()=>c.cancelSave,music_load:s=>{c.loaded.push(s.id);s.state=2;s.data=Buffer.alloc(8192);s.total=s.bytes=8192;},loaded:[],
 music_save:()=>{},vTaskDelay:()=>{},pdMS_TO_TICKS:n=>n,cancelSave:false};
vm.createContext(c);
function install(name,args,code=body(name)){vm.runInContext(`function ${name}(${args}){${translate(code)}}`,c);}
install('music_acquire','id');install('music_release','slot');
install('m4_tube_music_prepare','current,next',body('m4_tube_music_prepare').replace('memset(&music_slots[i],0,sizeof(music_slots[i]))','memset(music_slots[i],0,sizeof(music_slots[i]))'));
let worker=body('music_cache_worker').replace('for (;;)','for (var pass=0;pass<1;++pass)')
 .replace('char ids[2][12],save[12],title[31];','var ids=["",""],save="",title="";')
 .replace(/memcpy\((ids\[\d\]|save|title),([^;]+),(?:12|31)\);/g,'$1=$2;');
install('music_cache_worker','',worker);
const a='aaaaaaaaaaa',b='bbbbbbbbbbb',d='ddddddddddd';
c.m4_tube_music_prepare(a,b);c.music_cache_worker();c.music_cache_worker();assert.deepEqual(c.loaded,[a,b]);
const current=c.music_acquire(a);assert.equal(current.refs,1);const data=current.data;
c.m4_tube_music_prepare(b,d);c.music_cache_worker();assert.equal(current.data,data,'pinned current song cannot be evicted');assert.equal(c.loaded.length,2);
c.music_release(current);c.music_cache_worker();assert.equal(c.loaded.at(-1),d);assert(c.freed.includes(data));
assert(c.music_acquire(b),'prefetched next is promoted without fetching again');
assert.equal(c.loaded.filter(id=>id===b).length,1);
c.music_slots[0]={...blank(),id:a,state:3};c.m4_tube_music_prepare(a,b);assert.equal(c.music_slots[0].state,0,'explicit replay retries a failed current song');

// Cached playback never reads HTTP or SD; seek discards decoded PCM, not arbitrary MP3 bytes.
let play=body('music_cached_run').replaceAll('*ended','ended.value')
 .replace('m4_tube_music_cache_t state;','var state={};').replace('m4_tube_music_cache(id,&state)','m4_tube_music_cache(id,state)')
 .replace(/esp_audio_simple_dec_(cfg|raw|out|info)_t /g,'var ').replaceAll('&decoder','decoderOut')
 .replace(/esp_audio_simple_dec_open\(&?config,decoderOut\)/,'(decoder=decoderHandle,decoderStatus)');
const split=play.indexOf('\ndone:');assert(split>0);
play='try{'+play.slice(0,split).replaceAll('goto done;','throw FINISH;')+'}catch(e){if(e!==FINISH)throw e;}'+play.slice(split+6);
install('music_cached_run','id,seconds,out,ended,error,error_size',play);
Object.assign(c,{FINISH:{},decoderHandle:{},decoderStatus:0,paused:false,now:0,player:{},
 esp_timer_get_time:()=>c.now,m4_tube_music_cache:(_id,out)=>Object.assign(out,{loading:false,message:'failed'}),
 heap_caps_malloc:()=>c.noMemory?null:Buffer.alloc(32768),m4_audio_decoders_ready:()=>true,
 esp_audio_simple_dec_process:(_dec,raw,frame)=>{c.decodes++;raw.consumed=Math.min(raw.len,128);frame.decoded_size=c.noOutput?0:4096;if(c.decodeError)return -1;if(c.overrun)raw.consumed=raw.len+1;if(c.stall)raw.consumed=0;return 0;},
 esp_audio_simple_dec_get_info:(_dec,info)=>{Object.assign(info,{sample_rate:32000,bits_per_sample:16,channel:c.badFormat?1:2});return 0;},
 esp_audio_simple_dec_close:()=>c.decoderClosed++,player_update:(_buffering,_percent,ms)=>c.position=ms,
 vTaskDelay:()=>{c.now+=1000;if(c.cancelDecode)c.cancelled=true;},
 out:{cancelled:()=>c.cancelled,begin:rate=>{assert.equal(rate,32000);c.opened++;return !c.openFail;},
 write:(_pcm,bytes)=>{c.written+=bytes;return !c.writeFail;},drain:()=>!c.drainFail,message:()=>{}}});
function playback(options={},seconds=0){
 Object.assign(c,{noMemory:false,noOutput:false,decodeError:false,overrun:false,stall:false,badFormat:false,openFail:false,writeFail:false,drainFail:false,cancelDecode:false,cancelled:false,
  now:0,decodes:0,decoderClosed:0,opened:0,written:0},options);
 c.music_slots=[{...blank(),id:a,state:2,data:Buffer.alloc(8192),bytes:8192,total:8192},blank()];
 const ended={value:false},error={value:''};const handled=c.music_cached_run(a,seconds,c.out,ended,error,128);
 assert.equal(c.music_slots[0].refs,0,'playback releases its pin');return {handled,ended:ended.value,error:error.value};
}
assert(playback().ended);assert.equal(c.written,64*4096);assert.equal(c.opened,1);
assert(playback({},1).ended);assert.equal(c.written,(64*1024-32000)*4,'seek keeps samples after requested timestamp');
assert(!playback({noMemory:true}).handled,'allocation failure permits PCM fallback');
assert(!playback({badFormat:true}).handled);assert(!playback({decodeError:true}).handled);assert(!playback({overrun:true}).handled);
assert(!playback({writeFail:true}).ended);assert(!playback({drainFail:true}).ended);assert(!playback({cancelDecode:true}).ended);
assert(playback({},3).ended,'seek to end completes without replaying samples');assert.equal(c.written,0);
assert.equal(c.decoderClosed,1);
let follow=body('music_follow').replace('char next[12]="";','var next="";')
 .replace('atomic_load(&music_auto_next)','autoNext').replace('memcpy(next,music_wanted[1],sizeof(next))','next=music_wanted[1]');
install('music_follow','id',follow);
Object.assign(c,{autoNext:true,followed:[],m4_audio_tube_follow:id=>{c.followed.push(id);return true;}});
c.music_wanted=[a,b];c.music_follow(a);assert.deepEqual(c.followed,[b]);
c.autoNext=false;c.music_follow(a);assert.equal(c.followed.length,1);c.autoNext=true;c.music_follow(d);assert.equal(c.followed.length,1,'obsolete pair cannot follow a manual selection');
const audio=fs.readFileSync(path.join(root,'main/m4_audio.c'),'utf8');
install('m4_audio_tube_follow','id',body('m4_audio_tube_follow',audio).replace('request_t request=','var request=').replace('&request','request'));
Object.assign(c,{requests:{},active_epoch:7,cancelled:()=>c.followCancelled,pdTRUE:1,PLAY_TUBE:6,
 followCancelled:false,queueBusy:false,xQueueSend:(_q,request)=>{if(c.queueBusy)return 0;c.enqueued=request;return 1;}});
assert(c.m4_audio_tube_follow(b));assert.equal(c.enqueued.epoch,7);assert(c.enqueued.aac);assert.equal(c.enqueued.name,b);
c.followCancelled=true;assert(!c.m4_audio_tube_follow(b));c.followCancelled=false;c.queueBusy=true;assert(!c.m4_audio_tube_follow(b),'queued user action is preserved');
const tube=fs.readFileSync(path.join(root,'main/m4_tube.c'),'utf8');assert(tube.includes('if (audio_only && ended && !out->cancelled()) music_follow(id)'));
assert(source.includes('MUSIC_CACHE_BYTES = 8 * 1024 * 1024')&&source.includes('MUSIC_CACHE_RESERVE = 4 * 1024 * 1024'));
assert(!/\blv_\w+\(/.test(source));assert(!body('music_cached_run').includes('esp_http_client_'));
assert(body('music_save').includes('if (!slot)')&&body('music_save').includes('/api/music/file?job=%s'),'SD streaming fallback for large songs or low PSRAM');
assert(body('music_save').indexOf('fsync(fileno(file))')<body('music_save').indexOf('rename(partial,final)'));
assert(body('music_load').includes('music_needed(slot->id)')&&body('music_load').includes('HTTP_METHOD_DELETE'));
let saving=body('music_save').replace('char name[64],final[160],partial[168],job[33]="",url[320];','var name="",final="",partial="",job="",url="";')
 .replace('struct stat st;','var st={};').replaceAll('&st','st').replace('esp_http_client_config_t config=','var config=')
 .replaceAll('SD_MEDIA_ROOT "/','SD_MEDIA_ROOT + "/').replaceAll('memcpy(job,music_slots[i].job,sizeof(job))','job=music_slots[i].job')
 .replace('esp_vfs_fat_info(SD_MEDIA_ROOT,&capacity,&available)','(available=freeSpace,ESP_OK)')
 .replace('lease_tls(1500,&owned)','(owned=leaseOK)');
const end=saving.indexOf('\ndone:');assert(end>0);
saving='try{'+saving.slice(0,end).replaceAll('goto done;','throw FINISH;')+'}catch(e){if(e!==FINISH)throw e;}'+saving.slice(end+6);
install('music_save','id,title',saving);
Object.assign(c,{ESP_OK:0,NET_MODE_STA_CONNECTED:1,ESP_ERR_HTTP_EAGAIN:11,EEXIST:17,errno:0,O_WRONLY:1,O_CREAT:2,O_EXCL:4,
 SD_MEDIA_ROOT:'/sdcard',M4_TUBE_URL:'http://fixture',M4_TUBE_TOKEN:'fixture-only',esp_crt_bundle_attach:()=>{},
 network_get_mode:()=>1,leaseOK:true,sd_media_admin_lock:()=>!c.failLock,sd_media_admin_unlock:()=>c.unlocks++,sd_media_changed:()=>c.changed++,
 mkdir:()=>0,stat:()=>c.existing?0:-1,unlink:name=>c.files.delete(name),open:name=>{c.files.set(name,Buffer.alloc(0));return {name};},
 fdopen:fd=>fd,close:()=>{},fclose:()=>0,fileno:f=>f,fflush:()=>c.failSync?-1:0,fsync:()=>0,
 fwrite:(data,_one,n,file)=>{if(c.failWrite)return 0;c.files.set(file.name,Buffer.concat([c.files.get(file.name),data.subarray(0,n)]));if(c.cancelWriting)c.cancelSave=true;return n;},
 rename:(a,b)=>{c.files.set(b,c.files.get(a));c.files.delete(a);return 0;},download_progress:()=>{},
 https_memory_exclusive_end:()=>{},esp_http_client_init:()=>({}),esp_http_client_set_header:()=>{},esp_http_client_open:()=>0,
 esp_http_client_fetch_headers:()=>0,esp_http_client_get_status_code:()=>200,esp_http_client_get_content_length:()=>8192,
 esp_http_client_set_timeout_ms:()=>{},esp_http_client_read:(_h,buf,n)=>{if(c.truncate)return 0;buf.fill(0x55,0,n);return n;},
 esp_http_client_is_complete_data_received:()=>c.truncate,esp_http_client_close:()=>{},esp_http_client_cleanup:()=>{}});
function save(options={}){
 Object.assign(c,{cancelSave:false,failLock:false,failWrite:false,failSync:false,existing:false,cancelWriting:false,truncate:false,
  changed:0,unlocks:0,freeSpace:10000000,files:new Map(),download_state:{busy:true,saved:false}},options);
 c.music_slots=[{...blank(),id:a,state:options.stream?3:2,data:options.stream?null:Buffer.alloc(8192,0x55),total:8192,job:'a'.repeat(32)},blank()];
 c.music_save_id=a;c.music_save(a,'곡');assert(!c.download_state.busy);assert.equal(c.music_slots[0].refs,0);assert.equal(c.music_save_id,'');
 return c.download_state;
}
assert(save().saved);assert.equal([...c.files.values()][0].length,8192);assert.equal(c.changed,1);
assert(save({stream:true}).saved,'SD uses bounded HTTP transfer when a full PSRAM copy is unavailable');
assert(save({existing:true}).saved);assert.equal(c.files.size,0,'completed file is never replaced');
for(const options of [{failLock:true},{failWrite:true},{failSync:true},{freeSpace:1},{cancelWriting:true},{stream:true,truncate:true}]){
 assert(!save(options).saved,JSON.stringify(options));assert.equal(c.files.size,0,'partial SD file removed');assert.equal(c.changed,0);
}
let loading=body('music_load').replace('char detail[96] = "";','var detail="";')
 .replace(', *buffer = NULL',', buffer = NULL')
 .replace('char url[320], job[33] = ""; sink_t sink = {0};','var url="",job="",sink={};')
 .replaceAll('M4_TUBE_URL "/','M4_TUBE_URL + "/').replace(/(?:const )?cJSON \*/g,'var ')
 .replace('esp_http_client_config_t config =','var config =').replaceAll('&sink','sink').replace('lease_tls(1500,&owned)','(owned=leaseOK)')
 .replace('data+bytes','data.subarray(bytes)').replace('memcpy(slot->job,job,sizeof(job))','slot.job=job')
 .replace(/copy_text\((\w+(?:->\w+)?),sizeof\([^)]*\),([^;]+)\);/g,'$1=($2)?.valuestring||"";');
const loadEnd=loading.indexOf('\ndone:');assert(loadEnd>0);
loading='try{'+loading.slice(0,loadEnd).replaceAll('goto done;','throw FINISH;')+'}catch(e){if(e!==FINISH)throw e;}'+loading.slice(loadEnd+6);
install('music_load','slot',loading);
Object.assign(c,{MUSIC_CACHE_BYTES:8*1024*1024,MUSIC_CACHE_RESERVE:4*1024*1024,HTTP_METHOD_POST:1,HTTP_METHOD_GET:2,HTTP_METHOD_DELETE:3,
 sink_event:()=>{},music_needed:()=>!c.cancelLoad,music_slot_progress:(s,message,bytes,total)=>Object.assign(s,{message,bytes,total}),
 cJSON_Parse:b=>JSON.parse(b.subarray(0,b.indexOf(0)).toString()),cJSON_GetObjectItem:(o,k)=>typeof o?.[k]==='number'?{valuedouble:o[k]}:typeof o?.[k]==='string'?{valuestring:o[k]}:null,
 cJSON_IsNumber:v=>typeof v?.valuedouble==='number',cJSON_IsString:v=>typeof v?.valuestring==='string',cJSON_Delete:()=>{},
 copy_text:()=>{throw Error('untranslated copy_text');},strspn:(s,chars)=>{let n=0;while(n<s.length&&chars.includes(s[n]))n++;return n;},
 esp_http_client_delete_header:()=>{},esp_http_client_set_method:()=>{},esp_http_client_set_url:()=>{},
 heap_caps_get_free_size:()=>c.lowMemory?1024:32*1024*1024,heap_caps_malloc:n=>Buffer.alloc(n),
 download_json:(_h,sink,buf,_url,method)=>{
  if(method===3){c.cancelledJobs++;return 200;}
  if(c.failStatus)return c.failStatus;
  const json=method===1?{job:'a'.repeat(32)}:{state:c.cancelPreparing?'preparing':'ready',bytes:c.songBytes,duration:2,title:'노래',channel:'Fixture',channel_id:''};
  const text=JSON.stringify(json);buf.fill(0);buf.write(text);sink.length=Buffer.byteLength(text);return method===1?202:200;
 },
 esp_http_client_get_content_length:()=>c.wrongLength?1:c.songBytes,
 esp_http_client_read:(_h,buf,n)=>{if(c.truncate)return 0;buf.fill(0x55,0,n);if(c.cancelReading)c.cancelLoad=true;return n;},
 vTaskDelay:ms=>{c.now+=ms*1000;if(c.cancelPreparing)c.cancelLoad=true;}});
function load(options={}){
 Object.assign(c,{cancelLoad:false,cancelReading:false,cancelPreparing:false,lowMemory:false,wrongLength:false,truncate:false,failStatus:0,
  songBytes:8192,cancelledJobs:0,now:0,freed:[]},options);
 const slot={...blank(),id:a,state:1};c.music_load(slot);return slot;
}
let loaded=load();assert.equal(loaded.state,2);assert.equal(loaded.data.length,8192);assert.equal(loaded.bytes,8192);assert.equal(loaded.title,'노래');assert(!c.freed.includes(loaded.data));
for(const options of [{failStatus:401},{failStatus:404},{lowMemory:true},{songBytes:9*1024*1024},{wrongLength:true},{truncate:true},{cancelReading:true},{cancelPreparing:true}]){
 loaded=load(options);assert.equal(loaded.state,3,JSON.stringify(options));assert.equal(loaded.data,null,'no partial cache is published');
 if(options.cancelPreparing)assert.equal(c.cancelledJobs,1,'obsolete PC preparation is cancelled');
}
console.log('PASS: actual C pair loading/promotion, pinned-buffer eviction protection, retry, MP3 playback/seek/EOF/cancellation/error cleanup and fallback policies (host mocks only).');

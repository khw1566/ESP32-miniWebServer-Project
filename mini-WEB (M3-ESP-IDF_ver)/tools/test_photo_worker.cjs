// Execute the photo worker C body using bounded file, SD-lock and JPEG mocks.
const fs=require('fs'),path=require('path'),vm=require('vm'),assert=require('assert');
const source=fs.readFileSync(path.join(__dirname,'../main/m3_photos.c'),'utf8');
function body(name){const m=new RegExp('\\b'+name+'\\([^)]*\\)\\s*\\{').exec(source);assert(m,name);
 let start=m.index+m[0].length,end=start,depth=1;while(depth){if(source[end]==='{')depth++;if(source[end]==='}')depth--;end++;}return source.slice(start,end-1);}
const statuses=['IDLE','LOADING','READY','EMPTY','BUSY','INVALID','NO_MEMORY','NO_SD'];
let translated=body('m3_photos_poll')
 .replace(/\b(?:unsigned|int64_t|size_t|long|bool|int) (\w+)/g,'var $1')
 .replace('FILE *file','var file').replace('uint8_t *input','var input').replaceAll('NULL','null')
 .replaceAll('(int)','').replaceAll('(unsigned)','').replaceAll('(size_t)','')
 .replace('char path[320]; snprintf(path, sizeof(path), PHOTO_ROOT "/%s", names[photo_index]);','var path=PHOTO_ROOT+"/"+names[photo_index];')
 .replace('jpeg_decode_memory_alloc_cfg_t cfg = {.buffer_direction = JPEG_DEC_ALLOC_INPUT_BUFFER};','var cfg={};')
 .replaceAll('&cfg','cfg').replaceAll('&capacity','null').replaceAll('&lock','null')
 .replace(/&(active|rescan|steps|epoch|paused)/g,'"$1"')
 .replaceAll('input + read','input.subarray(read)').replace("input[0] == 'B' && input[1] == 'M'",'input[0] == 66 && input[1] == 77');
function environment(options={}){
 const ctx={active:true,rescan:true,paused:false,steps:0,epoch:1,now:0,due:0,count:0,photo_index:0,names:[],state:{revision:0,name:''},
  slots:[{data:null,state:0},{data:null,state:0}],FREE:0,WRITING:1,READY:2,SHOWN:3,
  PHOTO_ROOT:'/sdcard/widgets/photos',PHOTO_FILE_LIMIT:4*1024*1024,M3_PHOTO_BYTES:432*480*2,MALLOC_CAP_SPIRAM:1,MALLOC_CAP_8BIT:2,
  input:Buffer.from([66,77,0,0,0,0,0,0]),reads:0,opens:0,closes:0,locks:0,unlocks:0,freed:[],canvases:[],decoded:[],scans:0,
  ...Object.fromEntries(statuses.map((name,i)=>['M3_PHOTO_'+name,i]))};
 Object.assign(ctx,{
  atomic_load:key=>ctx[key],atomic_exchange:(key,value)=>{const old=ctx[key];ctx[key]=value;return old;},
  atomic_store:(key,value)=>{ctx[key]=value;},
  esp_timer_get_time:()=>ctx.now,sd_media_ready:()=>!options.noSd,
  sd_media_admin_lock:()=>{if(options.busy)return false;ctx.locks++;return true;},sd_media_admin_unlock:()=>ctx.unlocks++,
  scan:()=>{ctx.scans++;ctx.names=options.empty?[]:['日本語.jpg','春の写真.bmp','c.jpg'];ctx.count=ctx.names.length;ctx.photo_index=0;},
  status:value=>{ctx.state.status=value;ctx.state.count=ctx.count;ctx.state.index=ctx.photo_index;ctx.state.revision++;},
  claim:()=>{for(let i=0;i<2;i++)if(ctx.slots[i].state===0||ctx.slots[i].state===2){ctx.slots[i].state=1;return i;}return -1;},
  heap_caps_malloc:size=>{if(options.canvasOom)return null;const b=Buffer.alloc(size);ctx.canvases.push(b);return b;},
  jpeg_alloc_decoder_mem:size=>options.inputOom?null:Buffer.alloc(size),
  fopen:filename=>{assert(filename.startsWith(ctx.PHOTO_ROOT+'/'));ctx.opens++;return options.openFail?null:{offset:0};},
  fseek:(file,position,whence)=>{if(options.seekFail)return -1;file.offset=whence===2?ctx.input.length:position;return 0;},
  SEEK_END:2,SEEK_SET:0,ftell:file=>options.tooBig?ctx.PHOTO_FILE_LIMIT+1:file.offset,
  fread:(target,one,n,file)=>{ctx.reads++;if(options.readFail)return 0;
   const got=ctx.input.copy(target,0,file.offset,file.offset+n);file.offset+=got;
   if(options.cancel){ctx.active=false;ctx.epoch++;}if(options.newStep){options.newStep=false;ctx.steps=1;ctx.epoch++;}return got;},
  fclose:()=>ctx.closes++,free:buffer=>{if(buffer)ctx.freed.push(buffer);},
  cancelled:generation=>!ctx.active||generation!==ctx.epoch,
  m3_photo_bmp_fit:(input,size,canvas)=>{ctx.decoded.push('bmp');canvas[0]=123;return !options.decodeFail;},
  jpeg_fit:(input,size,canvas)=>{ctx.decoded.push('jpg');canvas[0]=42;return !options.decodeFail;},
  portENTER_CRITICAL:()=>{},portEXIT_CRITICAL:()=>{},m3_utf8_copy:(out,capacity,text)=>{ctx.state.name=text;return text.length;},
 });
 vm.createContext(ctx);vm.runInContext('function poll(){'+translated.replace('sizeof(state.name)','192')+'}',ctx);
 return ctx;
}
let ctx=environment();ctx.poll();assert.equal(ctx.state.status,ctx.M3_PHOTO_READY);assert.equal(ctx.state.name,'日本語.jpg');
assert.deepEqual(ctx.decoded,['bmp']);assert.equal(ctx.slots[0].state,ctx.READY);assert.equal(ctx.slots[0].data[0],123);
assert.equal(ctx.locks,ctx.unlocks);assert.equal(ctx.opens,ctx.closes);assert.equal(ctx.freed.length,1);
ctx.slots[0].state=ctx.SHOWN;ctx.now=1000000;ctx.poll();assert.equal(ctx.reads,1,'no premature auto advance');
ctx.now=8000000;ctx.poll();assert.equal(ctx.photo_index,1);assert.equal(ctx.slots[0].state,ctx.SHOWN);assert.equal(ctx.slots[1].state,ctx.READY);
ctx.paused=true;ctx.now=16000000;ctx.poll();assert.equal(ctx.photo_index,1,'paused slideshow waits');
ctx.steps=-1;ctx.poll();assert.equal(ctx.photo_index,0,'manual previous works while paused');
ctx.active=false;ctx.rescan=true;const reads=ctx.reads;ctx.poll();assert.equal(ctx.reads,reads,'hidden widget performs no file I/O');
for(const [options,status] of [[{busy:true},'BUSY'],[{noSd:true},'NO_SD'],[{empty:true},'EMPTY'],[{canvasOom:true},'NO_MEMORY'],
 [{inputOom:true},'NO_MEMORY'],[{readFail:true},'INVALID'],[{seekFail:true},'INVALID'],[{tooBig:true},'INVALID'],[{decodeFail:true},'INVALID']]){
 ctx=environment(options);ctx.poll();assert.equal(ctx.state.status,ctx['M3_PHOTO_'+status],JSON.stringify(options));
 assert.equal(ctx.locks,ctx.unlocks,'SD lock cleanup');assert.equal(ctx.closes,ctx.opens,'file cleanup');
 assert(!ctx.slots.some(slot=>slot.state===ctx.WRITING),'slot cleanup');
}
ctx=environment({openFail:true});ctx.poll();assert.equal(ctx.state.status,ctx.M3_PHOTO_INVALID);assert.equal(ctx.closes,0);assert.equal(ctx.locks,ctx.unlocks);
ctx=environment({cancel:true});ctx.poll();assert(!ctx.slots.some(s=>s.state===ctx.READY));assert.equal(ctx.freed.length,1);assert.equal(ctx.locks,ctx.unlocks);
ctx=environment({newStep:true});ctx.poll();assert.equal(ctx.steps,1,'new input remains pending after cancelling old decode');
ctx.poll();assert.equal(ctx.state.status,ctx.M3_PHOTO_READY);assert.equal(ctx.photo_index,1);assert.equal(ctx.freed.length,2);
ctx=environment();ctx.input=Buffer.from([66]);ctx.poll();assert.equal(ctx.decoded.length,0,'one-byte image cannot read input[1]');
ctx=environment();ctx.input=Buffer.from([0xff,0xd8,0,0]);ctx.poll();assert.deepEqual(ctx.decoded,['jpg']);
console.log('PASS: actual photo-worker interval/pause/manual step/hidden behavior, SD-busy/no-SD/empty states, read/seek/open/decode/OOM cleanup, short-file bounds, cancellation generation and immutable shown canvas.');

// Actual C helpers/scan/mailbox/UI construction with host SD/LVGL objects. No compiler or hardware codec.
const fs=require('fs'),path=require('path'),vm=require('vm'),assert=require('assert');
const root=path.resolve(__dirname,'..'),read=p=>fs.readFileSync(path.join(root,p),'utf8');
const core=read('main/m3_gallery.c'),index=read('main/m3_gallery_index.inc'),ui=read('main/m3_gallery_ui.inc');
function body(source,name){const m=new RegExp('\\b'+name+'\\([^;]*?\\)\\s*\\{').exec(source);assert(m,name);let start=m.index+m[0].length,i=start,d=1;while(d){if(source[i]==='{')d++;if(source[i]==='}')d--;i++;}return source.slice(start,i-1);}
const common=s=>s.replace(/\/\*[\s\S]*?\*\//g,'').replaceAll('->','.').replace(/\((?:unsigned|size_t|uint64_t|uint32_t|int|unsigned char)\)/g,'').replace(/\b(?:unsigned|size_t|int|bool) /g,'var ').replace(/(\d+)u\b/g,'$1');
const c={NULL:null,strlen:s=>Buffer.byteLength(s),strrchr:(s,ch)=>s?.includes(ch)?s.slice(s.lastIndexOf(ch)):null,strcasecmp:(a,b)=>a.toLowerCase()===b.toLowerCase()?0:1,
 m3_utf8_valid:s=>!/[\uD800-\uDFFF]/.test(s)};
vm.createContext(c);
for(const name of ['m3_gallery_supported','m3_gallery_video','m3_gallery_media']){
 let helper=body(core,name).replace('const char *ext','var ext').replace(/static const char \*const formats\[\] = \{([^}]+)\};/,'var formats=[$1];').replace('sizeof(formats) / sizeof(formats[0])','formats.length');
 vm.runInContext('function '+name+'(name){'+common(helper)+'}',c);
}
let valid=body(index,'m3_gallery_path_valid').replace('for (const char *p = path; *p; ++p)','for (var i=0,p=path.charCodeAt(0); i<path.length; p=path.charCodeAt(++i))')
 .replace('path[0]', 'path.charCodeAt(0)').replaceAll('p[1]','path.charCodeAt(i+1)').replaceAll('(unsigned char)*p','p').replaceAll('*p','p');
valid=valid.replace(/'(?:\\.|[^'\\])'/g,x=>String(x==="'\\\\'"?92:x.charCodeAt(1)));
vm.runInContext('function valid(path){'+valid+'}',c);
for(const p of ['/photo.JPG','/앨범/旅行.png','/a..b.jpg'])assert(c.valid(p));
for(const p of ['photo.jpg','/../a.jpg','/.private/a.png','/a/.cache/p.jpg','/a//b.jpg','/a\\b.jpg','/a:b.jpg','/a/','/a\nb.jpg','/'+ 'x'.repeat(255)])assert(!c.valid(p),p);
for(const n of ['a.JPG','b.jpeg','c.png','d.bmp','a.MP4','a.avi','a.mjpeg','a.mjpg'])assert(c.m3_gallery_supported(n));
for(const n of ['a.txt','a.mp3','a.wav','a.lua','a.json','a.mp4.part','a.png.exe'])assert(!c.m3_gallery_supported(n));
assert(c.m3_gallery_video('a.MP4')&&!c.m3_gallery_video('b.png'));
for(const name of ['photo.webp','photo.gif','photo.heic','clip.mov','clip.webm'])assert(c.m3_gallery_media(name)&&!c.m3_gallery_supported(name));
// Scan all nested media, ignoring nonmedia/hidden entries, with no per-directory or catalog-count cap.
const files={ '/sdcard':{dir:['first.jpg','sound.mp3','.private','nested','empty.png','notes.txt','phone.webp','animation.gif']},
 '/sdcard/.private':{dir:['secret.jpg']},'/sdcard/nested':{dir:['movie.MP4',...Array.from({length:20},(_,i)=>'image'+i+'.png'),'nested2']},
 '/sdcard/nested/nested2':{dir:['last.bmp']} };
for(const [p,v] of Object.entries({...files}))if(v.dir)for(const name of v.dir)files[p+'/'+name]??={};
const handles=[];
Object.assign(c,{library_next:{},library_dirs:Array(128).fill(null),library_lengths:Array(128).fill(0),M3_GALLERY_READY:2,M3_GALLERY_TILES:9,SD_MEDIA_ROOT:'/sdcard',
 library_current:()=>c.live,sd_media_ready:()=>true,sd_media_admin_lock:()=>true,sd_media_admin_unlock:()=>{},
 memset:o=>Object.assign(o,{count:0,shown:0,done_mask:0,error_mask:0,items:Array.from({length:9},()=>({}))}),
 opendir:p=>{if(!files[p]?.dir)return null;const d={path:p,at:0,closed:false};handles.push(d);return d;},
 readdir:d=>{assert(!d.closed);const name=files[d.path].dir[d.at++];return name?{d_name:name}:null;},closedir:d=>{assert(!d.closed);d.closed=true;},
 stat:(p,st)=>{if(!files[p])return -1;st.st_mode=files[p].dir?'dir':'file';st.st_size=0;return 0;},
 S_ISDIR:m=>m==='dir',S_ISREG:m=>m==='file',pdMS_TO_TICKS:n=>n,vTaskDelay:()=>{}});
c.m3_gallery_path_valid=c.valid;
let scan=body(index,'library_scan').replace('char relative[256] = ""','var relative = ""')
 .replace('char child[256], path[272]; struct stat st;','var child,path,st={};')
 .replace('int n = snprintf(child, sizeof(child), "%s/%s", relative, entry->d_name);','child=relative+"/"+entry.d_name; var n=Buffer.byteLength(child);')
 .replace('sizeof(child)','256').replace('snprintf(path, sizeof(path), "%s%s", SD_MEDIA_ROOT, child)','path=SD_MEDIA_ROOT+child')
 .replace('memcpy(relative, child, n + 1)','relative=child').replace('memcpy(item->path, child, n + 1)','item.path=child')
 .replace('relative[library_lengths[--depth]] = 0','relative=relative.slice(0,library_lengths[--depth])')
 .replace('m3_gallery_item_t *item = &library_next.items[library_next.shown++]','var item=library_next.items[library_next.shown++]')
 .replace('struct dirent *entry','var entry').replace(/DIR \*dir/g,'var dir').replace('&library_next','library_next').replace('sizeof(library_next)','0').replace('&st','st');
c.Buffer=Buffer;vm.runInContext('function scan(page,job){'+common(scan)+'}',c);
for(const page of [0,1,2]){c.live=true;assert(c.scan(page,7));assert.equal(c.library_next.count,26);assert.equal(c.library_next.shown,page===2?8:9);assert(handles.every(d=>d.closed));assert(c.library_next.items.slice(0,c.library_next.shown).every(o=>c.valid(o.path)&&c.m3_gallery_media(o.path)));}
assert(c.library_next.items.slice(0,8).some(o=>o.path.endsWith('/last.bmp')));
c.live=false;assert(!c.scan(0,7));assert(handles.every(d=>d.closed),'cancel closes all directory handles');
// Mailbox is pinned during the bulk copy and never exposes worker-owned storage to LVGL.
Object.assign(c,{thumbnail_owner:2,thumbnail_generation:7,thumbnail_index:1,thumbnail_mailbox:Buffer.alloc(28800,0x43),M3_GALLERY_THUMB_BYTES:28800,
 atomic_compare_exchange_strong:(name,expected,value)=>{if(c[name]!==expected)return false;c[name]=value;return true;},atomic_store:(name,value)=>c[name]=value,
 memcpy:(out,input,n)=>{assert.equal(c.thumbnail_owner,3);input.copy(out,0,0,n);}});
let copy=common(body(index,'m3_gallery_thumbnail_copy')).replaceAll('&thumbnail_owner','"thumbnail_owner"').replace('&expected','expected');
vm.runInContext('function copy(job,index,out,capacity){'+copy+'}',c);c.live=true;
const output=Buffer.alloc(28800);assert(!c.copy(7,0,output,output.length));assert.equal(c.thumbnail_owner,2);
assert(c.copy(7,1,output,output.length));assert.equal(c.thumbnail_owner,0);assert(output.every(v=>v===0x43));
c.thumbnail_owner=2;assert(!c.copy(8,1,output,output.length));assert.equal(c.thumbnail_owner,0,'stale generation releases the mailbox');
// Exact RGB565 source bytes survive the bounded square crop.
Object.assign(c,{M3_PHOTO_W:432,M3_PHOTO_H:480,M3_GALLERY_THUMB_W:120,M3_GALLERY_THUMB_H:120});
let scale=common(body(index,'thumbnail_scale')).replace('const uint8_t *','var ')
 .replace('y * edge / M3_GALLERY_THUMB_H','Math.floor(y * edge / M3_GALLERY_THUMB_H)').replace('x * edge / M3_GALLERY_THUMB_W','Math.floor(x * edge / M3_GALLERY_THUMB_W)');
vm.runInContext('function scale(canvas,out){'+scale+'}',c);
const canvas=Buffer.alloc(432*480*2);for(let y=0;y<480;y++)for(let x=0;x<432;x++)canvas.writeUInt16LE((x+y*13)&65535,(y*432+x)*2);
c.scale(canvas,output);for(const [x,y] of [[0,0],[60,60],[119,119]])assert.equal(output.readUInt16LE((y*120+x)*2),canvas.readUInt16LE(((24+Math.floor(y*432/120))*432+Math.floor(x*432/120))*2));
assert(!/fopen\(|fread\(|m3_photo_decode\(|m3_mp4_thumbnail\(/.test(ui),'UI only queues work and copies completed thumbnails');
assert(ui.includes('LV_SYMBOL_PLAY')&&ui.includes('gallery_show_path(gallery_catalog.items[index].path, index)'));
assert(!read('main/m3_phone_ui.inc').includes('gallery_rows'),'gallery no longer mirrors the file explorer');
assert(index.includes('MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT')&&ui.includes('static M3_PSRAM_BSS uint8_t gallery_thumb_pixels'));
const mp4=read('main/m3_mp4.c'),thumbnail=body(mp4,'m3_mp4_thumbnail');
assert(!/audio_track\(|xTaskCreate|audio->|wait_time\(/.test(thumbnail),'first-frame decoding never starts audio or a playback task');
const codec={M3_MP4_OK:0,M3_MP4_STOPPED:1,M3_MP4_MEMORY:4,M3_MP4_VIDEO_ERROR:5,ESP_H264_ERR_OK:0,ESP_H264_ERR_MEM:2,
 esp_h264_dec_get_resolution:(_,size)=>{Object.assign(size,codec.resolution);return 0;},
 esp_h264_dec_process:(_,input,output)=>{const step=codec.steps[codec.calls++];input.consume=step.consume;Object.assign(output,{out_size:step.bytes||0,outbuf:Buffer.alloc(step.bytes||0)});return step.error||0;},
 m3_mp4_i420_fit:(_data,bytes,_width,_height,out)=>{out[0]=77;return bytes>0;}};
let first=body(mp4,'thumbnail_decode').replace('esp_h264_dec_in_frame_t input = {.raw_data = {.buffer = data, .len = bytes}}','var input={raw_data:{buffer:data,len:bytes}}')
 .replace('esp_h264_dec_out_frame_t output = {0}','var output={out_size:0}').replace('esp_h264_resolution_t size = {0}','var size={}')
 .replace('esp_h264_err_t err','var err').replaceAll('&input','input').replaceAll('&output','output').replaceAll('&size','size').replace('*ready = true','ready.value=true');
first=common(first).replace('input.raw_data.buffer += input.consume','input.raw_data.buffer=input.raw_data.buffer.subarray(input.consume)');
vm.createContext(codec);vm.runInContext('function first(decoder,parameters,info,data,bytes,job,current,canvas,ready){'+first+'}',codec);
function decode(steps,current=()=>true,resolution={width:16,height:16}){Object.assign(codec,{steps,calls:0,resolution});const ready={},out=Buffer.alloc(4);return {result:codec.first({}, {},{width:16,height:16},Buffer.alloc(8),8,1,current,out,ready),ready,out,calls:codec.calls};}
let frame=decode([{consume:4,bytes:384},{consume:4,bytes:384}]);assert.equal(frame.result,0);assert(frame.ready.value);assert.equal(frame.out[0],77);assert.equal(frame.calls,1,'stop after the first decoded picture');
assert.equal(decode([{consume:0,bytes:0}]).result,5);assert.equal(decode([{consume:9,bytes:384}]).result,5);
assert.equal(decode([{consume:0,error:2}]).result,4);assert.equal(decode([],()=>false).result,1);
assert.equal(decode([{consume:4,bytes:384}],()=>true,{width:32,height:16}).result,5);
// Build the actual LVGL object tree and verify every thumbnail/control fits its parent.
const geometry={gallery_tiles:Array.from({length:9},()=>({})),M3_GALLERY_TILES:9,M3_GALLERY_THUMB_W:120,M3_GALLERY_THUMB_H:120,M3_PHOTO_W:432,M3_PHOTO_H:480};
const construct=body(ui,'build_gallery');
function node(parent,kind){const o={parent,kind,children:[],style:{}};parent.children.push(o);return o;}
for(const name of new Set([...construct.matchAll(/\b(lv_\w+)\(/g)].map(m=>m[1])))geometry[name]=()=>{};
for(const name of new Set([...construct.matchAll(/\b[A-Z][A-Z0-9_]*\b/g)].map(m=>m[0])))geometry[name]??=0;
for(const name of ['gallery_refresh','gallery_pick','gallery_page','gallery_library_open','gallery_step','gallery_pause'])geometry[name]=()=>{};
Object.assign(geometry,{NULL:null,LV_PCT:n=>({percent:n}),bare:p=>node(p,'layer'),label:p=>node(p,'label'),
 button:p=>node(p,'button'),icon_button:p=>node(p,'button'),refresh_button:p=>node(p,'button'),volume_row:p=>node(p,'volume'),
 lv_button_create:p=>node(p,'tile'),lv_canvas_create:p=>node(p,'canvas'),
 lv_obj_remove_style_all:o=>o.style={},lv_obj_set_pos:(o,x,y)=>Object.assign(o.style,{x,y}),
 lv_obj_set_size:(o,width,height)=>Object.assign(o.style,{width,height}),lv_obj_set_width:(o,width)=>o.style.width=width});
let construction=common(construct).replace(/lv_obj_t \*/g,'var ').replace('gallery_tile_t *item = &gallery_tiles[i]','var item=gallery_tiles[i]')
 .replace('(void *)(uintptr_t)','').replace(/\(void \*\)\(intptr_t\)/g,'').replace('i / 3','Math.floor(i / 3)');
vm.createContext(geometry);vm.runInContext('function construct(page,volume){'+construction+'}',geometry);
const viewport={style:{width:480,height:656},children:[]},page=node(viewport,'page');geometry.construct(page,40);
function dimension(o,key){const n=o.style[key]??100;return typeof n==='number'?n:dimension(o.parent,key)*n.percent/100;}
assert.equal(dimension(page,'width'),480);assert.equal(dimension(page,'height'),656);
assert.equal(new Set(geometry.gallery_tiles.map(o=>o.tile.style.x)).size,3);assert.equal(new Set(geometry.gallery_tiles.map(o=>o.tile.style.y)).size,3);
for(const o of geometry.gallery_tiles)for(const child of [o.tile,o.canvas,o.name,o.play]){
 assert((child.style.x||0)+dimension(child,'width')<=dimension(child.parent,'width'));
 assert((child.style.y||0)+dimension(child,'height')<=dimension(child.parent,'height'));
}
for(const child of geometry.gallery_viewer.children)if(child.style.x!==undefined&&child.style.width!==undefined){
 assert(child.style.x+dimension(child,'width')<=480);assert(child.style.y+dimension(child,'height')<=656);
}
const demux=read('main/m3_mp4_demux.c');assert(demux.includes('atomic_flag_test_and_set(&indexing)'));assert(!body(demux,'m3_mp4_demux_close').includes('allocating ='));
const pins={generation:2,NULL:null,slots:[{data:'stale',owner:2,generation:1},{data:'new',owner:2,generation:2}],
 portENTER_CRITICAL:()=>{},portEXIT_CRITICAL:()=>{},atomic_load:()=>pins.generation};
vm.createContext(pins);vm.runInContext('function frame(){'+common(body(core,'m3_gallery_frame')).replace('const uint8_t *data','var data').replaceAll('&lock','null').replace('&generation','null')+'}',pins);
assert.equal(pins.frame(),'new');assert.equal(pins.slots[0].owner,0);pins.generation=3;
assert.equal(pins.frame(),null,'previous media must never reappear after a new generation');
pins.slots[0]={data:'next',owner:2,generation:3};assert.equal(pins.frame(),'next');assert.equal(pins.slots[1].owner,0);
console.log('PASS: recursive media filtering/pagination, cancellation/ownership, exact RGB565 crop, first-picture/error/cancel decode, 3-column thumbnail and full-view geometry, UI/PSRAM contracts.');

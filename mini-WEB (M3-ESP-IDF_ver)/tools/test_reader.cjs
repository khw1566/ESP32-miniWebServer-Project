// Actual C policy/layout-controller bodies with host mocks. No compiler, SD or native LVGL rendering.
const fs=require('fs'),path=require('path'),vm=require('vm'),assert=require('assert');
const root=path.resolve(__dirname,'..'),read=p=>fs.readFileSync(path.join(root,p),'utf8').replace(/\r\n/g,'\n');
const core=read('main/m3_reader.c'),ui=read('main/m3_reader_ui.inc');
function body(source,name){
 const m=new RegExp('\\b'+name+'\\([^;]*?\\)\\s*\\{').exec(source);assert(m,name);
 let start=m.index+m[0].length,i=start,depth=1;
 while(depth){if(source[i]==='{')depth++;if(source[i]==='}')depth--;i++;}return source.slice(start,i-1);
}
// LVGL stores dimensions in local styles: clearing styles must not shrink the reader's clip parent.
const screenSource=read('main/m3_ui.c');
const layout={st_page:{padding:16},st_bare:{padding:0},LV_OBJ_FLAG_SCROLLABLE:1,
 LV_SCROLLBAR_MODE_ACTIVE:1,LV_DIR_VER:1,LV_OPA_COVER:255,
 LV_PCT:n=>({percent:n}),lv_obj_create:parent=>({parent,style:{},flags:1}),
 lv_obj_remove_style_all:o=>{o.style={};},lv_obj_add_style:(o,s)=>Object.assign(o.style,s),
 lv_obj_remove_flag:(o,f)=>{o.flags&=~f;},
 lv_obj_set_size:(o,w,h)=>Object.assign(o.style,{width:w,height:h}),
 lv_obj_set_pos:(o,x,y)=>Object.assign(o.style,{x,y}),
 lv_obj_set_scrollbar_mode:()=>{},lv_obj_set_scroll_dir:()=>{},lv_obj_set_style_bg_opa:()=>{}};
function dimension(o,axis){
 const value=o.style[axis]??100;
 return typeof value==='number'?value:dimension(o.parent,axis)*value.percent/100;
}
vm.createContext(layout);
vm.runInContext('function app_page(parent){'+body(screenSource,'app_page')
 .replace('lv_obj_t *page','const page').replace('&st_page','st_page')+'}',layout);
// bare() creates a style-free, non-scrolling object; only its geometry matters in this fixture.
layout.bare=parent=>({parent,style:{padding:0},flags:0});
vm.runInContext('function construct(page){'+body(ui,'build_reader').split('reader_canvas =')[0]+'}',layout);
const viewport={style:{width:480,height:656}},page=layout.app_page(viewport);
for(const height of [656,712,656]){
 viewport.style.height=height;layout.construct(page);
 assert.equal(dimension(page,'width'),480,'reader root must fill the viewport after removing styles');
 assert.equal(dimension(page,'height'),height,'reader root must retain full height');
 assert.equal(dimension(layout.reader_surface,'width'),480,'paper must fill the root');
 assert.equal(dimension(layout.reader_surface,'height'),height,'paper must follow viewport height');
 assert.equal(page.flags&layout.LV_OBJ_FLAG_SCROLLABLE,0,'page turns must not scroll the root');
 const options=layout.app_page(page);
 assert.equal(dimension(options,'width'),480,'settings must share the full reader width');
 assert.equal(dimension(options,'height'),height,'settings must share the full reader height');
}
const c={NULL:null,strlen:s=>Buffer.byteLength(s),strrchr:(s,ch)=>s.includes(ch)?s.slice(s.lastIndexOf(ch)):null,
 strcasecmp:(a,b)=>a.toLowerCase()===b.toLowerCase()?0:1,
 m3_utf8_valid:s=>{try{new TextDecoder('utf8',{fatal:true}).decode(typeof s==='string'?Buffer.from(s):s.subarray(0,s.indexOf(0)));return true;}catch{return false;}},
 memchr:(b,value,n)=>b.subarray(0,n).includes(value),abs:Math.abs};
vm.createContext(c);
vm.runInContext('function supported(name){'+body(core,'m3_reader_supported').replace('const char *ext','const ext')+'}',c);
c.m3_reader_supported=c.supported;
const defaults={version:1,font_px:22,theme:1,letter_space:0,line_space:8,margin:24,background:0,font_family:0};
c.M3_READER_FONT_COUNT=3;
vm.runInContext('function valid(s){'+body(core,'m3_reader_settings_valid').replaceAll('s->','s.')+'}',c);
assert(c.valid(defaults));
for(let family=0;family<3;family++)assert(c.valid({...defaults,font_family:family}));
for(const [field,values] of Object.entries({version:[0,2],font_px:[0,15,37,255],theme:[4,255],letter_space:[7],line_space:[25],margin:[0,11,41],background:[3],font_family:[3,255]}))
 for(const value of values)assert(!c.valid({...defaults,[field]:value}),field+'='+value);
for(const name of ['a.txt','책.TXT','notes.md','server.log'])assert(c.supported(name));
for(const name of ['a.epub','a.pdf','a.txt.exe','a','a.png'])assert(!c.supported(name));
let validPath=body(core,'m3_reader_path_valid')
 .replace('for (const char *p = path; *p; ++p)','for (let i=0,p=path.charCodeAt(0); i<path.length; p=path.charCodeAt(++i))')
 .replace('path[0]', 'path.charCodeAt(0)').replaceAll('p[1]','path.charCodeAt(i+1)')
 .replaceAll('(unsigned char)*p','p').replaceAll('*p','p');
validPath=validPath.replace(/'(?:\\.|[^'\\])'/g,x=>String(x==="'\\\\'"?92:x.charCodeAt(1)));
vm.runInContext('function pathValid(path){'+validPath+'}',c);
for(const p of ['/book.txt','/books/한글.TXT','/books/nested/a.md'])assert(c.pathValid(p),p);
for(const p of ['book.txt','/../key.txt','/.private/key.txt','/books/./a.txt','/books/../a.txt','//a.txt','/a\\b.txt','/a:b.txt','/a\nb.txt','/'+ 'a'.repeat(255)+'.txt'])assert(!c.pathValid(p),p);
let norm=body(core,'m3_reader_normalize').replace(/\bsize_t\s+/g,'let ').replace(/\bunsigned char\s+/g,'let ')
 .replaceAll('(unsigned char)','').replaceAll('(char)','').replace('*length = used','length.value = used');
norm=norm.replace(/'(?:\\.|[^'\\])'/g,x=>String(x==="'\\r'"?13:x==="'\\n'"?10:x==="'\\t'"?9:x.charCodeAt(1)));
vm.runInContext('function normalize(text,bytes,length){'+norm+'}',c);
function normalize(input){const bytes=Buffer.from(input),out=Buffer.alloc(bytes.length+1);bytes.copy(out);const length={};return {ok:c.normalize(out,bytes.length,length),text:out.subarray(0,length.value).toString('utf8')};}
assert.deepEqual(normalize('\ufeff한글\r\nEnglish\r다음\t줄\n'),{ok:true,text:'한글\nEnglish\n다음 줄\n'});
for(const text of ['', '\ufeff', Buffer.from([0xff,0xfe,0x41,0]),'x\u0000y','x\u0001y',Buffer.from([0xc0,0xaf]),Buffer.from([0xf0,0x9f])])assert(!normalize(text).ok);
vm.runInContext('function direction(dx,dy,x,y,width,height,drag){'+body(ui,'reader_direction').replace('int ax','let ax')+'}',c);
assert.equal(c.direction(-90,15,0,0,432,526,true),1);assert.equal(c.direction(90,-10,0,0,432,526,true),-1);
assert.equal(c.direction(10,-90,0,0,432,526,true),1);assert.equal(c.direction(-10,90,0,0,432,526,true),-1);
assert.equal(c.direction(10,12,0,0,432,526,true),0);
for(const [x,y,want] of [[10,250,-1],[420,250,1],[216,10,-1],[216,510,1],[216,263,0],[-1,200,0]])assert.equal(c.direction(0,0,x,y,432,526,false),want);
// Execute the actual render loop with a byte-based wrapping mock for mixed UTF-8 text.
c.reader_window=Buffer.alloc(8192);c.reader_generation=1;c.reader_offset=0;c.reader_next=0;c.reader_config={...defaults};
c.reader_snapshot={status:2,bytes:0};c.M3_READER_READY=2;c.M3_WIDTH=480;c.LV_TEXT_FLAG_NONE=0;c.LV_ANIM_OFF=0;
c.reader_text={};c.reader_position={};c.reader_seek={};c.reader_dirty=true;
let document,progress=[];const decoder=new TextDecoder('utf8',{fatal:true});
c.m3_reader_copy=(g,offset,out,cap)=>{let n=Math.min(document.length-offset,cap-1);while(n&&(document[offset+n]&0xc0)===0x80)n--;document.copy(out,0,offset,offset+n);out[n]=0;return n;};
c.m3_reader_font=px=>({line_height:Math.ceil(34*px/24)});
c.reader_boundary=(b,i,n)=>{while(i<n&&(b[i]&0xc0)===0x80)i++;return i;};
c.lv_text_get_next_line=(b,font,space,width)=>{let bytes=0,count=0;for(const ch of decoder.decode(b.subarray(0,b.indexOf(0)))){bytes+=Buffer.byteLength(ch);if(ch==='\n'||++count>=Math.floor(width/(16+space)))break;}return bytes;};
c.lv_label_set_text=(o,b)=>{o.value=decoder.decode(b.subarray(0,b.indexOf(0)));};
c.set_text=(o,s)=>{o.value=s;};c.lv_slider_set_value=(o,n)=>{o.value=n;};c.m3_reader_progress=(g,n)=>progress.push(n);
let render=body(ui,'reader_render').replace(/\b(const )?(size_t|unsigned)\s+/g,'let ')
 .replace('const lv_font_t *font','const font').replace('sizeof(reader_window)','reader_window.length')
 .replace('reader_window + used','reader_window.subarray(used)').replace('char text[32]','let text')
 .replace('snprintf(text, sizeof(text), "%u%%", progress)','text=progress+"%"').replace(/\(uint64_t\)|\(unsigned\)/g,'')
 .replace(/(\d+)u\b/g,'$1').replace("reader_window[used - 1] == '\\n'",'reader_window[used - 1] == 10');
render=render.replace('(526 + reader_config.line_space) / (font->line_height + reader_config.line_space)',
 'Math.trunc((526 + reader_config.line_space) / (font.line_height + reader_config.line_space))').replaceAll('font->','font.');
vm.runInContext('function render(){'+render+'}',c);
document=Buffer.from(('한글 문장과 English words are preserved.\n\n'+ '긴문장'.repeat(70)+'\n').repeat(45));c.reader_snapshot.bytes=document.length;
for(const family of [0,1,2])for(const px of [16,22,24,36]){
 c.reader_offset=0;let pages=0;
 c.reader_config={...defaults,font_family:family,font_px:px,margin:px===16?40:12,letter_space:family===1?6:0,line_space:family===2?24:0};
 while(c.reader_offset<document.length){
  const start=c.reader_offset;c.render();
  assert(c.reader_next>start&&c.reader_next<=document.length);
  const consumed=decoder.decode(document.subarray(start,c.reader_next));
  assert.equal(c.reader_text.value,consumed.replace(/\n+$/,''),'page payload preserves every mixed-text byte');
  assert.equal(progress.at(-1),start);c.reader_offset=c.reader_next;
  if(++pages>1000)throw Error('No bounded progress');
 }
 assert(pages>10);assert.equal(c.reader_position.value,'100%');
}
// Actual snapshot-copy body: reject stale generations and partial UTF-8 offsets.
const copyContext={state:{status:2,generation:7,bytes:0},M3_READER_READY:2,M3_READER_WINDOW:8192,
 memcpy:(out,src,n)=>src.copy(out,0,0,n)};
vm.createContext(copyContext);
const copied=body(core,'m3_reader_copy').replace(/\bsize_t\s+/g,'let ').replaceAll('(unsigned char)','')
 .replace(/port(?:ENTER|EXIT)_CRITICAL\(&lock\);/g,'').replace('document + offset','document.subarray(offset)');
vm.runInContext('function copy(generation,offset,out,capacity){'+copied+'}',copyContext);
const sample=Buffer.from('A한글B한글C');copyContext.document=Buffer.concat([sample,Buffer.from([0])]);copyContext.state.bytes=sample.length;
for(let cap=2;cap<=sample.length+2;cap++){
 const out=Buffer.alloc(cap);const n=copyContext.copy(7,0,out,cap);assert(n<cap);decoder.decode(out.subarray(0,n));assert.equal(out[n],0);
}
assert.equal(copyContext.copy(6,0,Buffer.alloc(16),16),0,'stale request cannot access replaced book');
assert.equal(copyContext.copy(7,2,Buffer.alloc(16),16),0,'continuation-byte start rejected');
copyContext.state.status=1;assert.equal(copyContext.copy(7,0,Buffer.alloc(16),16),0,'loading retires the old document');
// The renderer and paginator must both wrap. LVGL 9.2 CLIP sets expand=1.
assert(body(ui,'build_reader').includes('lv_label_set_long_mode(reader_text, LV_LABEL_LONG_WRAP)'));
assert(!/lv_label_set_long_mode\(reader_text, LV_LABEL_LONG_CLIP\)/.test(ui));
assert(body(ui,'reader_configure_ui').includes('reader_config.font_family = segment_value(reader_family)'));
// Execute the actual integer resampler on padded A8 rows, including one-pixel edges.
const fontSource=read('main/m3_font_reader.c');
const fontContext={};
vm.createContext(fontContext);
let scaled=body(fontSource,'scaled').replaceAll('(int)','')
 .replaceAll('source_px / 2','Math.trunc(source_px / 2)')
 .replace('(value * pixels + Math.trunc(source_px / 2)) / source_px','Math.trunc((value * pixels + Math.trunc(source_px / 2)) / source_px)')
 .replace('((-value * pixels + Math.trunc(source_px / 2)) / source_px)', '(Math.trunc((-value * pixels + Math.trunc(source_px / 2)) / source_px))');
vm.runInContext('function scaled(value,pixels,source_px){'+scaled+'}',fontContext);
assert.equal(fontContext.scaled(24,36,24),36);assert.equal(fontContext.scaled(-4,36,24),-6);
let interpolation=body(fontSource,'interpolate').replace(/\b(?:unsigned|int)\s+/g,'let ')
 .replaceAll('(unsigned)','')
 .replace('((2 * x + 1) * width * 128) / target_w','Math.trunc(((2 * x + 1) * width * 128) / target_w)')
 .replace('((2 * y + 1) * height * 128) / target_h','Math.trunc(((2 * y + 1) * height * 128) / target_h)');
vm.runInContext('function interpolate(data,stride,width,height,x,y,target_w,target_h){'+interpolation+'}',fontContext);
const raster=[10,20,30,255,40,50,60,255];
for(const width of [1,2,3,6,9])for(const height of [1,2,4,7]){
 for(let y=0;y<height;y++)for(let x=0;x<width;x++){
  const sx=Math.max(0,(x+.5)*3/width-.5),sy=Math.max(0,(y+.5)*2/height-.5);
  const x0=Math.floor(sx),y0=Math.floor(sy),x1=Math.min(2,x0+1),y1=Math.min(1,y0+1);
  const fx=sx-x0,fy=sy-y0;
  const expected=(raster[y0*4+x0]*(1-fx)+raster[y0*4+x1]*fx)*(1-fy)+(raster[y1*4+x0]*(1-fx)+raster[y1*4+x1]*fx)*fy;
  assert(Math.abs(fontContext.interpolate(raster,4,3,2,x,y,width,height)-expected)<1,'smooth resampling ignores stride padding');
  assert.equal(fontContext.interpolate([123,255,255,255],4,1,1,x,y,width,height),123);
 }
}
const choose=body(fontSource,'font_at');
assert(choose.includes('&fonts[family][pixels - 16]'),'sizes and families have stable font identities');
assert(body(fontSource,'trim_atlases').includes('i == active_family || i == system_family'),'reader and system faces remain available together');
assert(/MALLOC_CAP_SPIRAM \| MALLOC_CAP_8BIT/.test(core));
assert(body(core,'load').includes('sd_media_admin_lock()')&&body(core,'load').includes('sd_media_admin_unlock()'));
assert(body(core,'load').includes('fclose(file)')&&body(core,'publish').includes('free(old)'));
assert(body(core,'publish').includes('r->generation == atomic_load(&epoch)'));
assert(body(core,'worker').includes('pdMS_TO_TICKS(2000)'));
assert(!/nvs_|fopen|fread|fseek/.test(ui),'UI never performs file/NVS I/O');
assert(body(ui,'reader_configure_ui').includes('reader_history_count = 0')&&!body(ui,'reader_configure_ui').includes('reader_offset = 0'),'reflow preserves anchor');
assert(read('main/m3_ui.c').includes('reader_open_path(snapshot.folder, entry->name)'));
assert(read('main/CMakeLists.txt').includes('"m3_reader.c"')&&read('main/CMakeLists.txt').includes('"m3_font_reader.c"'));
console.log('PASS: reader viewport/paper/settings bounds, settings/path/UTF-8 normalization, four-way gestures/edge taps, mixed-text byte pagination, cancellation/ownership and SD/NVS task boundaries (host mocks only).');

// Host execution of calendar/pixel/ownership/UI policies; hardware JPEG and SD are mocked.
const fs=require('fs'),path=require('path'),vm=require('vm'),assert=require('assert');
const root=path.resolve(__dirname,'..'),read=p=>fs.readFileSync(path.join(root,p),'utf8').replace(/\r\n/g,'\n');
function body(source,name){const m=new RegExp('\\b'+name+'\\([^)]*\\)\\s*\\{').exec(source);assert(m,name);
 let start=m.index+m[0].length,end=start,d=1;while(d){if(source[end]==='{')d++;if(source[end]==='}')d--;end++;}return source.slice(start,end-1);}
const calendar=read('main/m3_calendar.c'),c={};vm.createContext(c);
const common=s=>s.replace(/\b(?:unsigned|uint32_t|uint16_t|size_t|int32_t|int|int64_t|bool) /g,'let ')
 .replace(/\(uint(?:16|32|64)_t\)|\(int64_t\)/g,'').replaceAll('NULL','null').replace(/lv_obj_t \*/g,'let ');
for(const name of ['m3_calendar_days','m3_calendar_first']){
 let code=body(calendar,name).replace(/static const unsigned (\w+)\[\] = \{([^}]+)\};/g,'const $1=[$2];');
 code=common(code).replace(/year \/ (400|100|4)\b/g,'Math.floor(year / $1)');
 vm.runInContext(`function ${name}(year,month){${code}}`,c);
}
for(let year=1970;year<=2199;year++)for(let month=1;month<=12;month++){
 assert.equal(c.m3_calendar_days(year,month),new Date(Date.UTC(year,month,0)).getUTCDate());
 assert.equal(c.m3_calendar_first(year,month),new Date(Date.UTC(year,month-1,1)).getUTCDay());
}
assert.equal(c.m3_calendar_days(2100,2),28);assert.equal(c.m3_calendar_days(2000,2),29);
vm.runInContext('function shift(date,direction){'+body(calendar,'m3_calendar_shift').replace('!year || !month || ','')
 .replaceAll('*year','date.year').replaceAll('*month','date.month')+'}',c);
let date={year:2026,month:12};c.shift(date,1);assert.deepEqual(date,{year:2027,month:1});c.shift(date,-1);assert.equal(date.month,12);
date={year:1970,month:1};c.shift(date,-1);assert.equal(date.year,1970);date={year:2199,month:12};c.shift(date,1);assert.equal(date.year,2199);

const pixels=read('main/m3_photo_pixels.c'),W=432,H=480,BYTES=W*H*2;
const p={M3_PHOTO_W:W,M3_PHOTO_H:H,M3_PHOTO_BYTES:BYTES,memset:(data,value,n)=>data.fill(value,0,n)};vm.createContext(p);
vm.runInContext('function le16(p){'+common(body(pixels,'le16'))+'}\nfunction le32(p){return (le16(p) | le16(p.subarray(2)) << 16) >>> 0;}\nfunction dimensions(width,height){'+common(body(pixels,'dimensions'))+'}',p);
let fit=common(body(pixels,'fit')).replaceAll('*w','out.w').replaceAll('*h','out.h')
 .replace('height * M3_PHOTO_W / width','Math.floor(height * M3_PHOTO_W / width)')
 .replace('width * M3_PHOTO_H / height','Math.floor(width * M3_PHOTO_H / height)');
vm.runInContext('function fit(width,height,out){'+fit+'}',p);
function pixelBody(name){return common(body(pixels,name))
 .replace('let w, h; fit(width, height, &w, &h);','let out={}; fit(width,height,out); let w=out.w,h=out.h;')
 .replaceAll('(M3_PHOTO_W - w) / 2','Math.floor((M3_PHOTO_W - w) / 2)')
 .replaceAll('(M3_PHOTO_H - h) / 2','Math.floor((M3_PHOTO_H - h) / 2)')
 .replaceAll('y * height / h','Math.floor(y * height / h)').replaceAll('x * width / w','Math.floor(x * width / w)')
 .replace(/le(16|32)\(data \+ (\d+)\)/g,'le$1(data.subarray($2))')
 .replace('(int32_t)le32(data.subarray(22))','(le32(data.subarray(22))|0)')
 .replace("data[0] != 'B' || data[1] != 'M'","data[0] != 66 || data[1] != 77")
 .replace('~3u','~3').replace('const uint8_t *p = data + offset + sy * stride + (Math.floor(x * width / w)) * pixel;',
 'let p = data.subarray(offset + sy * stride + Math.floor(x * width / w) * pixel);');}
vm.runInContext('function bmp(data,size,canvas){'+pixelBody('m3_photo_bmp_fit')+'}\nfunction rgb(data,width,height,stride,canvas){'+pixelBody('m3_photo_rgb565_fit')+'}',p);
function bmpFixture(topDown=false,bits=24){const w=2,h=2,px=bits/8,stride=(w*px+3)&~3,b=Buffer.alloc(54+stride*h);
 b.write('BM');b.writeUInt32LE(b.length,2);b.writeUInt32LE(54,10);b.writeUInt32LE(40,14);b.writeUInt32LE(w,18);b.writeInt32LE(topDown?-h:h,22);b.writeUInt16LE(1,26);b.writeUInt16LE(bits,28);
 const colors=[[255,0,0],[0,255,0],[0,0,255],[255,255,255]];
 for(let y=0;y<h;y++)for(let x=0;x<w;x++){const at=54+(topDown?y:1-y)*stride+x*px,[r,g,blue]=colors[y*2+x];b[at]=blue;b[at+1]=g;b[at+2]=r;}return b;}
const at=(b,x,y)=>b[y*W*2+x*2]|b[y*W*2+x*2+1]<<8;
for(const top of [false,true])for(const bits of [24,32]){
 const input=bmpFixture(top,bits),out=new Uint8Array(BYTES);assert(p.bmp(input,input.length,out));
 assert.equal(at(out,0,0),0);assert.equal(at(out,20,40),0xf800);assert.equal(at(out,400,40),0x07e0);
 assert.equal(at(out,20,430),0x001f);assert.equal(at(out,400,430),0xffff);
}
const raw=Buffer.from([0,0xf8,0xe0,7,31,0,255,255]);const out=new Uint8Array(BYTES);assert(p.rgb(raw,2,2,4,out));assert.equal(at(out,400,430),0xffff);
for(const mutate of [b=>b.writeUInt32LE(1,30),b=>b.writeUInt32LE(0xffffffff,18),b=>b.writeUInt32LE(0xfffffffe,10),b=>b.writeInt32LE(-2147483648,22),b=>b.writeUInt16LE(8,28)]){
 const input=bmpFixture();mutate(input);assert(!p.bmp(input,input.length,new Uint8Array(BYTES)));
}
assert(!p.bmp(Buffer.alloc(2),2,out));assert(!p.bmp(bmpFixture(),54,out));assert(!p.rgb(raw,0,2,4,out));

const photos=read('main/m3_photos.c'),q={slots:[{data:'a',state:3},{data:'b',state:0}],FREE:0,WRITING:1,READY:2,SHOWN:3,
 portENTER_CRITICAL:()=>{},portEXIT_CRITICAL:()=>{}};vm.createContext(q);
const slotBody=name=>common(body(photos,name)).replaceAll('&lock','null').replace('const uint8_t *frame','let frame');
vm.runInContext('function claim(){'+slotBody('claim')+'}\nfunction frame(){'+slotBody('m3_photos_frame')+'}',q);
assert.equal(q.claim(),1);assert.equal(q.slots[0].state,q.SHOWN);assert.equal(q.frame(),'a','writer cannot overwrite displayed canvas');
q.slots[1].state=q.READY;assert.equal(q.frame(),'b');assert.equal(q.slots[0].state,q.FREE);assert.equal(q.claim(),0);

const ui=read('main/m3_ui.c'),active=[];
const main=body(ui,'build_home_main'),home=body(ui,'build_home');
assert(home.indexOf('home_wallpaper = bare(home)')<home.indexOf('home_tiles = lv_tileview_create(home)'));
assert(main.indexOf('home = foreground;')<main.indexOf('home_clock = label(home'));
assert(home.includes('m3_wallpaper_build(home_wallpaper, ui_theme.wallpaper, CYAN, MAGENTA)'));
const wallpaper=read('main/m3_wallpaper.c');
assert(wallpaper.includes('shape(holder, -140, 300')&&wallpaper.includes('shape(holder, 250, -120'),'default wallpaper keeps the original orbs');
assert(home.includes('lv_obj_set_style_bg_opa(home_sheets[i], LV_OPA_TRANSP, 0)'));
assert(!home.includes('T_HOME_MORE')&&!home.includes('T_HOME_SLIDE'),'extra apps have no heading or swipe caption');
assert(!main.includes('icon_part(home,'),'decorative shapes cannot be siblings above widgets');
// Build the real main tile body into a mock object tree to check the draw hierarchy.
const tree={children:[]},layer={home_clock:null,home_date:null,home_net_icon:null,home_net:null,home_detail:null};
function node(parent,kind){const obj={parent,kind,children:[]};parent.children.push(obj);return obj;}
for(const name of new Set([...main.matchAll(/\b(lv_\w+)\(/g)].map(m=>m[1])))layer[name]=()=>{};
for(const name of new Set([...main.matchAll(/\b[A-Z][A-Z0-9_]*\b/g)].map(m=>m[0])))layer[name]=0;
Object.assign(layer,{m3_font_clock:{},lv_font_montserrat_20:{},launch_app:()=>{},bare:parent=>node(parent,'layer'),
 label_ellipsis:()=>{},
 icon_part:parent=>node(parent,'orb'),label:(parent,text)=>node(parent,'label'),
 lv_button_create:parent=>node(parent,'status-card'),lv_label_create:parent=>node(parent,'label'),
 home_apps:parent=>node(parent,'app-icons')});
vm.createContext(layer);vm.runInContext('function build(home){'+common(main).replace(/\(void \*\)\(uintptr_t\)/g,'')
 .replaceAll('&m3_font_clock','m3_font_clock').replaceAll('&lv_font_montserrat_20','lv_font_montserrat_20')+'}',layer);layer.build(tree);
assert.equal(tree.children.length,1);const [foreground]=tree.children;
assert.equal(layer.home_clock.parent,foreground);assert.equal(layer.home_date.parent,foreground);
assert.equal(layer.home_net.parent.parent,foreground);
assert(foreground.children.some(obj=>obj.kind==='app-icons'),'apps share the foreground layer');
// The actual shared background constructor must put every decorative shape below the tileview.
layer.lv_tileview_create=parent=>node(parent,'tileview');
for(const name of new Set([...home.matchAll(/\b(lv_\w+)\(/g)].map(m=>m[1])))layer[name]??=()=>{};
for(const name of new Set([...home.matchAll(/\b[A-Z][A-Z0-9_]*\b/g)].map(m=>m[0])))layer[name]??=0;
layer.LV_PCT=n=>n;layer.ui_theme={wallpaper:0};layer.m3_wallpaper_build=holder=>node(holder,'orb');
const shared={children:[]};vm.runInContext('function background(home){'+common(home.split('for (unsigned i = 0; i < HOME_PAGE_COUNT; ++i)')[0])+'}',layer);
layer.background(shared);assert.deepEqual(shared.children.map(obj=>obj.kind),['layer','tileview']);
assert(shared.children[0].children.every(obj=>obj.kind==='orb'));
const appCount=/typedef enum \{ (APP_HOME[^}]+) \} app_id_t/.exec(ui)[1].split(',').map(s=>s.trim()).indexOf('APP_COUNT');
const primary=+(/HOME_PRIMARY_APPS = (\d+)/.exec(ui)[1]),perPage=+(/HOME_APPS_PER_PAGE = (\d+)/.exec(ui)[1]);
const photoIndex=2+Math.ceil((appCount-1-primary)/perPage),pageCount=photoIndex+1;
const nav={home_tiles:{},home_sheets:Array.from({length:pageCount},()=>({})),home_index:1,home_dots:Array.from({length:pageCount},()=>({})),home_page_name:{},HOME_PAGE_COUNT:pageCount,HOME_PHOTOS:photoIndex,APP_HOME:0,current_app:0,CYAN:1,
 lv_obj_get_child:obj=>obj,lv_obj_set_width:(o,w)=>o.width=w,lv_obj_set_style_bg_color:()=>{},lv_color_hex:n=>n,lv_obj_center:()=>{},
 tr:n=>n,home_page_title:n=>'page-'+n,set_text:(o,s)=>o.text=s,m3_photos_activate:enabled=>active.push(enabled),theme_wallpaper_sync:()=>{},lv_tileview_get_tile_active:()=>nav.home_sheets[nav.selected]};
vm.createContext(nav);vm.runInContext('function update(){'+common(body(ui,'home_page_update'))+'}\nfunction changed(event){'+common(body(ui,'home_tile_changed'))
 .replace('(void)event;','').replace('lv_obj_t *active','let active').replace('home_page_update()','update()')+'}',nav);
for(const index of [0,1,...Array.from({length:photoIndex-1},(_,i)=>i+2),2,1]){nav.selected=index;nav.changed(null);assert.equal(nav.home_index,index);assert.equal(active.at(-1),index===photoIndex);assert.equal(nav.home_dots[index].width,24);}
nav.current_app=7;nav.selected=photoIndex;nav.changed(null);assert.equal(active.at(-1),false,'an app keeps photo I/O suspended');
assert(ui.includes('lv_obj_add_flag(home_sheets[i], LV_OBJ_FLAG_SCROLL_CHAIN_HOR)'));
assert(ui.includes('lv_obj_set_scroll_snap_x(home_tiles, LV_SCROLL_SNAP_CENTER)'));
assert(photos.includes('MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT'));assert(!photos.includes('xTaskCreate'));
assert(photos.includes('#include "freertos/FreeRTOS.h"'),'photo spinlock APIs have their defining header');
assert(read('main/m3_runtime.c').includes('m3_photos_poll();'));assert(!/fopen\(|opendir\(|jpeg_decoder_process/.test(body(ui,'update_photos')));
const cmake=read('main/CMakeLists.txt');for(const file of ['m3_photos.c','m3_photo_pixels.c','m3_calendar.c'])assert(cmake.includes('"'+file+'"'));
const api=read('managed_components/lvgl__lvgl/src/widgets/tileview/lv_tileview.h');
for(const name of ['lv_tileview_create','lv_tileview_add_tile','lv_tileview_set_tile_by_index','lv_tileview_get_tile_active'])assert(api.includes(name+'(')||api.includes(name+' ('));
console.log('PASS: 1970-2199 Gregorian months/leap years, date navigation, 24/32bit BMP orientation/bounds, RGB565 aspect fit, immutable displayed slots, horizontal page selection/photo suspension, task/PSRAM/API integration.');

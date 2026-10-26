// Actual C policy/controller bodies with NVS/LVGL mocks. No native build/render.
const fs=require('fs'),path=require('path'),vm=require('vm'),assert=require('assert');
const root=path.resolve(__dirname,'..'),read=p=>fs.readFileSync(path.join(root,p),'utf8').replace(/\r\n/g,'\n');
const prefs=read('main/m3_preferences.c'),ui=read('main/m3_ui.c'),font=read('main/m3_font_reader.c');
function body(source,name){const m=new RegExp('\\b'+name+'\\([^;]*?\\)\\s*\\{').exec(source);assert(m,name);
 let i=m.index+m[0].length,start=i,depth=1;while(depth){if(source[i]==='{')depth++;if(source[i]==='}')depth--;i++;}return source.slice(start,i-1);}
const defaults=()=>({version:1,family:0,pixels:16,reserved:0});
const c={typography:defaults(),typography_revision:0,typography_dirty:false,typography_next_save:0,
 storage_lock:1,now:0,failed:false,writes:[],committed:null,M3_SYSTEM_FONT_MIN:16,M3_SYSTEM_FONT_MAX:24,
 NVS_READWRITE:1,ESP_OK:0,pdTRUE:1,esp_timer_get_time:()=>c.now,
 xSemaphoreTake:()=>1,xSemaphoreGive:()=>{},ESP_LOGW:()=>{},esp_err_to_name:String,
 memcmp:(a,b)=>JSON.stringify(a)===JSON.stringify(b)?0:1,nvs_open:()=>0,
 nvs_set_blob:(_h,key,p)=>{assert.equal(key,'typography');c.writes.push({...p});return c.failed?5:0;},
 nvs_commit:()=>{if(c.failed)return 5;c.committed={...c.writes.at(-1)};c.duringCommit?.();return 0;},nvs_close:()=>{}};
vm.createContext(c);
for(const [name,args] of [['m3_typography_valid','p'],['m3_typography_get','out'],['m3_typography_update','p'],['m3_typography_poll','']]){
 let s=body(prefs,name).replace(/port(?:ENTER|EXIT)_CRITICAL\(&lock\);/g,'')
  .replace('m3_typography_t p;','let p={};').replace('nvs_handle_t nvs;','let nvs={};')
  .replace(/\b(?:unsigned|bool|esp_err_t)\s+/g,'let ')
  .replaceAll('&typography','typography').replaceAll('&nvs','nvs').replaceAll('&p','p')
  .replace('typography = *p','typography = {...p}').replace('*out = typography','Object.assign(out,typography)')
  .replace('p = typography','p = {...typography}').replaceAll('->','.')
  .replaceAll('sizeof(typography)','4').replaceAll('sizeof(p)','4');
 vm.runInContext('function '+name+'('+args+'){'+s+'}',c);
}
for(const family of [0,1,2])for(let pixels=16;pixels<=24;pixels++)assert(c.m3_typography_valid({...defaults(),family,pixels}));
for(const patch of [{version:2},{family:3},{family:255},{pixels:15},{pixels:25},{reserved:1}])assert(!c.m3_typography_valid({...defaults(),...patch}));
c.m3_typography_update({...defaults(),family:2,pixels:24});assert(c.typography_dirty);assert.equal(c.writes.length,0,'UI changes perform no NVS I/O');
c.m3_typography_poll();assert.deepEqual(c.committed,{...defaults(),family:2,pixels:24});assert(!c.typography_dirty);
c.m3_typography_update({...defaults(),family:1,pixels:20});c.failed=true;c.now=2000000;c.m3_typography_poll();
assert(c.typography_dirty);assert.equal(c.committed.family,2,'failed commit retains the saved choice');
c.failed=false;c.now=4000000;c.m3_typography_poll();assert.equal(c.committed.family,1);assert(!c.typography_dirty);
c.m3_typography_update({...defaults(),pixels:18});c.duringCommit=()=>c.m3_typography_update({...defaults(),pixels:22});
c.now=6000000;c.m3_typography_poll();assert(c.typography_dirty,'a concurrent UI edit remains pending after an older write');
c.duringCommit=null;c.now=8000000;c.m3_typography_poll();assert.equal(c.committed.pixels,22);assert(!c.typography_dirty);
assert(body(prefs,'m3_preferences_init').includes('type_length == sizeof(type) && m3_typography_valid(&type)'));
assert(prefs.includes('sizeof(m3_preferences_t) == 8'),'original settings layout is unchanged');

// The exact same immutable font instances are used by root text and live preview.
const apply=body(ui,'apply_typography'),build=body(ui,'build_settings');
assert(apply.includes('lv_obj_set_style_text_font(screen, font, 0)'));
assert(apply.includes('lv_obj_set_style_text_font(font_preview, font, 0)'));
assert(build.includes('lv_label_set_long_mode(font_preview, LV_LABEL_LONG_WRAP)'));
assert(!/fopen|nvs_/.test(body(ui,'typography_changed')));
for(const filename of ['m3_ui.c','m3_reader_ui.inc','m3_ytmusic_ui.inc','m3_phone_ui.inc','m3_gallery_ui.inc','m3_flash_usage_view.c'])
 assert(!read('main/'+filename).includes('LV_LABEL_LONG_SCROLL'),filename+' must not scroll text');
const layout={LV_LABEL_LONG_DOT:1,LV_PART_MAIN:0,
 lv_label_set_long_mode:(o,mode)=>o.mode=mode,
 lv_obj_get_style_text_font:o=>({line_height:o.line}),lv_obj_set_height:(o,h)=>o.height=h};
vm.createContext(layout);
vm.runInContext('function ellipsis(obj){'+body(ui,'label_ellipsis').replace('->line_height','.line_height')+'}',layout);
for(const line of [23,26,29,34,40]){const o={line,width:120,height:300};layout.ellipsis(o);assert.equal(o.height,line);assert.equal(o.mode,1);assert.equal(o.width,120);}
const job={kind:'label',mode:1,line:29,height:58};
Object.assign(layout,{lv_label_class:'label',lv_textarea_class:'textarea',job_labels:[job,null,null,null,null,null],
 lv_obj_check_type:(o,kind)=>o.kind===kind,lv_textarea_get_one_line:o=>o.one_line,
 lv_label_get_long_mode:o=>o.mode,lv_obj_get_child_count:o=>o.children?.length??0,lv_obj_get_child:(o,i)=>o.children[i]});
let lines=body(ui,'typography_lines').replace(/\b(?:bool|unsigned|int)\s+/g,'let ').replaceAll('&lv_label_class','lv_label_class')
 .replaceAll('&lv_textarea_class','lv_textarea_class').replaceAll('->line_height','.line_height');
vm.runInContext('function typography_lines(obj){'+lines+'}',layout);
const input={kind:'textarea',one_line:true,line:34,height:52},name={kind:'label',mode:1,line:34,height:100};
const preview={kind:'label',mode:0,line:34,height:120};layout.typography_lines({children:[input,name,job,preview]});
assert.equal(input.height,62,'large fonts retain input padding');assert.equal(name.height,34);
assert.equal(job.height,58,'service cards keep two lines');assert.equal(preview.height,120,'preview text retains wrapping');
assert(body(ui,'create_shade').includes('icon_button(job_cards[i], LV_SYMBOL_STOP, service_stop'));
assert(!body(ui,'create_shade').includes('T_SERVICE_STOP,'));

// Execute atlas ownership: switching the reader must not evict system data.
const cache={M3_READER_FONT_COUNT:3,active_family:0,system_family:0,NULL:null,
 reader_faces:Array.from({length:3},(_,i)=>({blob:{data:i+1}})),free:()=>{},font_at:(px,family)=>({px,family})};
vm.createContext(cache);
let trim=body(font,'trim_atlases').replace(/\bunsigned\s+/g,'let ').replaceAll('->','.');
vm.runInContext('function trim_atlases(){'+trim+'}',cache);
for(const name of ['m3_reader_font','m3_system_font'])
 vm.runInContext('function '+name+'(pixels,family){'+body(font,name)+'}',cache);
cache.m3_system_font(20,1);assert.equal(cache.reader_faces[0].blob.data,1);assert.equal(cache.reader_faces[1].blob.data,2);assert.equal(cache.reader_faces[2].blob.data,null);
cache.reader_faces[2].blob.data=3;cache.m3_reader_font(32,2);
assert.equal(cache.reader_faces[1].blob.data,2);assert.equal(cache.reader_faces[2].blob.data,3);assert.equal(cache.reader_faces[0].blob.data,null);
cache.m3_system_font(24,1);assert.equal(cache.reader_faces[2].blob.data,3,'system size changes retain reader data');
cache.m3_reader_font(18,0);assert.equal(cache.reader_faces[1].blob.data,2);assert.equal(cache.reader_faces[2].blob.data,null);
console.log('PASS: 3 system faces at 16..24px, live renderer preview, separate NVS persistence/retry/concurrent edits, no marquee, bounded ellipsis, icon service controls and shared reader/system atlas ownership (host mocks only).');

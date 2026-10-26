// Execute bounded C parser/framing/ownership helpers with host mocks. No compiler.
const fs=require('node:fs'),path=require('node:path'),vm=require('node:vm'),assert=require('node:assert/strict');
const root=path.resolve(__dirname,'..'),read=f=>fs.readFileSync(path.join(root,f),'utf8');
function body(source,name){const m=new RegExp('\\b'+name+'\\([^)]*\\)\\s*\\{').exec(source);assert(m,name);
 let start=m.index+m[0].length,end=start,depth=1;while(depth){if(source[end]==='{')depth++;if(source[end]==='}')depth--;end++;}return source.slice(start,end-1);}
const calc=read('main/m4_calculator.c');
function js(text){return text.replace(/\/\*[\s\S]*?\*\//g,'').replaceAll('->','.')
 .replace(/\((?:unsigned char|unsigned|int|size_t)\)/g,'')
 .replace(/\b(?:double|bool|char|unsigned|int|size_t) (\w+)/g,'let $1');}
const context={strcmp:(a,b)=>a===b?0:1,strlen:s=>s.length,isfinite:Number.isFinite,
 isalpha:c=>/[a-z]/i.test(c||'')&&!!c,isdigit:c=>/[0-9]/.test(c||''),
 ...Object.fromEntries(['sin','cos','tan','asin','acos','atan','sqrt','exp','floor','pow','log','log10'].map(k=>[k,Math[k]])),fabs:Math.abs,
 strtod:s=>{const m=/^(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?/.exec(s);return {value:m?Number(m[0]):0,length:m?m[0].length:0};}};
vm.createContext(context);
for(const name of ['spaces','primary','power','unary','term','expression']){
 let code=body(calc,name);
 if(name==='primary')code=code.replace(/char \*end;[\s\S]*?p->pos = \(unsigned\)\(end - p->text\);/,
 'const parsed = strtod(p->text.slice(p->pos)); value = parsed.value; if (!parsed.length) p->error = true; p->pos += parsed.length;')
 .replace('char name[12] = {0};','let name = "";').replace('sizeof(name)','12')
 .replace('name[n++] = p->text[p->pos++];','{ name += p->text[p->pos++]; ++n; }');
 vm.runInContext('function '+name+'(p){'+js(code)+'}',context);
}
function evaluate(input,degrees=true,answer=0){if(!input||input.length>192)return null;
 const p={text:input,pos:0,depth:0,degrees,error:false,answer};const result=context.expression(p);context.spaces(p);
 return p.error||p.text[p.pos]||!Number.isFinite(result)?null:result;}
for(const [input,expected] of [['2+3*4',14],['(2+3)*4',20],['-2^2',-4],['2^-3',.125],['2^3^2',512],['5!',120],['50%',.5],
 ['sin(30)',.5],['cos(60)',.5],['sqrt(81)^2+sin(30)',81.5],['log(100)+ln(e)',3],['asin(1)',90],['abs(-3)',3],['1e-3+2',2.001]])
 assert(Math.abs(evaluate(input)-expected)<1e-9,input);
assert(Math.abs(evaluate('sin(pi/2)',false)-1)<1e-12);assert.equal(evaluate('Ans*2',true,9),18);
for(const invalid of ['1/0','sqrt(-1)','ln(0)','171!','2.5!','tan(90)','1+','(2','sin30','x(2)','2 3','1e999','('.repeat(30)+'1'+')'.repeat(30),'-'.repeat(30)+'1','2^'.repeat(30)+'2'])assert.equal(evaluate(invalid),null,invalid);
const gallery=read('main/m4_gallery.c');
const frames={slots:[{data:'first',owner:0},{data:'second',owner:0}],lock:null,
 portENTER_CRITICAL:()=>{},portEXIT_CRITICAL:()=>{},NULL:null};vm.createContext(frames);
for(const name of ['claim','m4_gallery_frame']){
 const code=js(body(gallery,name)).replace('const uint8_t *data','let data').replaceAll('&lock','lock');
 vm.runInContext('function '+name+'(){'+code+'}',frames);
}
assert.equal(frames.claim(),0);assert.equal(frames.claim(),1);assert.equal(frames.claim(),-1);
frames.slots[0].owner=2;assert.equal(frames.m4_gallery_frame(),'first');assert.equal(frames.claim(),-1,'shown buffer is pinned');
frames.slots[1].owner=2;assert.equal(frames.m4_gallery_frame(),'second');assert.equal(frames.claim(),0,'previous shown buffer becomes writable only after handoff');
const framing={JPEG_LIMIT:256*1024,EOF:-1,current:()=>true,fgetc:f=>f.pos<f.data.length?f.data[f.pos++]:-1};vm.createContext(framing);
vm.runInContext('function next_jpeg(file,buffer,job){'+js(body(gallery,'next_jpeg'))+'}',framing);
function frame(bytes){return {data:Buffer.from(bytes),pos:0};}
let output=Buffer.alloc(256*1024),file=frame([0,1,255,216,1,2,255,217,7,255,216,3,255,217]);
assert.equal(framing.next_jpeg(file,output,1),6);assert.deepEqual([...output.subarray(0,6)],[255,216,1,2,255,217]);
assert.equal(framing.next_jpeg(file,output,1),5);assert.equal(framing.next_jpeg(file,output,1),0);
assert.equal(framing.next_jpeg(frame([255,216,1,2]),output,1),0,'truncated JPEG rejected');
assert.equal(framing.next_jpeg(frame([255,216,...Array(256*1024).fill(1)]),output,1),0,'frame bound enforced');
framing.current=()=>false;assert.equal(framing.next_jpeg(frame([255,216,255,217]),output,1),0,'cancelled job stops reads');
const storage=read('main/storage_view.c');
const paths={strlen:s=>Buffer.byteLength(s),m4_utf8_valid:()=>true,strchr:(s,c)=>s.includes(typeof c==='number'?String.fromCharCode(c):c)};vm.createContext(paths);
let validation=body(storage,'mutation_path').replace('unsigned char c = path[i];','let c = path.charCodeAt(i) || 0;').replace("c == '/'","c == 47");
vm.runInContext('function valid(path){'+js(validation)+'}',paths);
for(const valid of ['/media/a.jpg','/recordings/rec.wav.part','/문서/자료.txt','/folder/a b.png'])assert(paths.valid(valid),valid);
for(const invalid of ['','/','relative','/../x','/a/../x','/a//b','/a/','/.upload.tmp','/a\\b','/a:b','/a.','/a ','/a\n','/'+ 'x'.repeat(256)])assert(!paths.valid(invalid),invalid);
assert(storage.includes('!stat(to, &st) || errno != ENOENT'),'destination must not exist');
assert(storage.includes('rmdir(from)'),'directory deletion is non-recursive');
assert(storage.includes('memcmp(header + 8, expected + 8, 32)'),'recovery verifies PCM format');
assert(storage.indexOf('fclose(file)')<storage.indexOf('return rename(from, to)'),'recovery closes before publishing');
const ui=read('main/m4_ui.c'),phone=read('main/m4_phone_ui.inc');
const declaration='static lv_obj_t *list_row(lv_obj_t *parent, lv_event_cb_t callback, void *data);';
assert(ui.indexOf(declaration)>=0&&ui.indexOf(declaration)<ui.indexOf('#include "m4_phone_ui.inc"'),'declare list_row before the gallery calls it');
const appIds=/typedef enum \{ ([^}]+) \} app_id_t;/.exec(ui)[1].split(',').map(s=>s.trim()).filter(s=>!['APP_HOME','APP_COUNT'].includes(s));
const iconTable=/\} app_icons\[\] = \{([\s\S]*?)\n\};/.exec(ui)[1];
const iconIds=[...iconTable.matchAll(/\{(APP_\w+),/g)].map(m=>m[1]);
assert.equal(new Set(iconIds).size,iconIds.length,'each app has one unique icon');
assert.deepEqual(iconIds.slice().sort(),appIds.slice().sort(),'every app is available on Home');
const launcher=body(ui,'home_apps');
assert(!launcher.includes('apps[]'),'launcher uses the icon table as its single app list');
assert(launcher.includes('(void *)(uintptr_t)app_icons[i].app'));
assert(launcher.includes('app_names[app_icons[i].app]'));
assert(ui.includes('LV_EVENT_SHORT_CLICKED'),'long press must not also play a file');
assert(phone.includes('if (!delete_armed)'),'destructive UI action needs a second explicit tap');
assert(!phone.includes('boot_info = text('),'deleted splash objects cannot remain in translation bindings');
console.log('PASS: C scientific parser, precedence/angles/domain/depth, gallery JPEG bounds/cancel/frame ownership, file path validation and mutation safeguards. Host mocks; no native decode/display.');

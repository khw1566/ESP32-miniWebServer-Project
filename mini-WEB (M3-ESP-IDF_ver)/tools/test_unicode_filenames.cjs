// Execute generated LVGL descriptors/bitmap conversion and UTF-8 C bodies in host mocks.
// No native compiler, firmware or PC UI renderer is invoked.
const fs=require('fs'),path=require('path'),vm=require('vm'),assert=require('assert');
const root=path.resolve(__dirname,'..'),read=p=>fs.readFileSync(path.join(root,p),'utf8');
function body(source,name){const m=new RegExp('\\b'+name+'\\([^)]*\\)\\s*\\{').exec(source);assert(m,name);
 let start=m.index+m[0].length,end=start,depth=1;while(depth){if(source[end]==='{')depth++;if(source[end]==='}')depth--;end++;}return source.slice(start,end-1);}
function array(source,name){const m=new RegExp('\\b'+name+'\\[\\] = \\{([\\s\\S]*?)\\};').exec(source);assert(m,name);
 return m[1].split(',').map(v=>v.trim()).filter(Boolean).map(Number);}
const cjk=read('main/m3_font_cjk.c'),korean=read('main/m3_font_korean.c');
const points=array(cjk,'codepoints'),upper=array(cjk,'upper_codepoints'),offsets=array(cjk,'offsets'),bitmap=Uint8Array.from(array(cjk,'bitmap'));
const manifest=JSON.parse(read('third_party/unifont/subset-manifest.json'));
assert.equal(points.length+upper.length,manifest.glyph_count);assert.equal(bitmap.length,manifest.bitmap_bytes);
assert.equal(offsets.length,manifest.glyph_count+1);assert.equal(offsets.at(-1)*16,bitmap.length);
const font={codepoints:points,upper_codepoints:upper,offsets,LV_FONT_GLYPH_FORMAT_A1:1};vm.createContext(font);
function descriptor(source){return body(source,'describe').replace(/\(void\)(?:font|next);/g,'')
 .replace(/sizeof\((codepoints|upper_codepoints)\) \/ sizeof\(\1\[0\]\)/g,'$1.length')
 .replace(/\bunsigned /g,'let ').replaceAll('out->','out.').replace('(int)width','width')
 .replaceAll('(hi - lo) / 2','Math.floor((hi - lo) / 2)');}
vm.runInContext('function describe(cp,out){'+descriptor(cjk)+'}',font);
const dscs=new Map();for(const cp of [...points,...upper]){
 const d={gid:{}};assert(font.describe(cp,d),cp.toString(16));assert([8,16].includes(d.box_w));assert.equal(d.box_h,16);
 assert.equal(d.format,1);assert(d.gid.index+d.box_w*d.box_h/8<=bitmap.length);dscs.set(cp,d);
}
for(const cp of [0,65,0xAC00,0xD7A3,0x1F600,0x10FFFF])assert(!font.describe(cp,{gid:{}}));
assert.equal(dscs.get(0x3099).adv_w,0);assert.equal(dscs.get(0x309a).ofs_x,-16);
const joyo=JSON.parse(read('third_party/unifont/joyo-17.0.json')).codepoints.map(s=>parseInt(s,16));
for(const cp of joyo)assert(dscs.has(cp),'Joyo missing '+cp.toString(16));assert(dscs.has(0x20B9F),'supplementary Joyo 𠮟');
for(const text of ['春の音楽（特別版）①.mp3','ひらがな・カタカナ.wav','韓國漢字 音樂.aac','ｶﾀｶﾅ','Café £10 €5 ±×÷','𠮟る・夏の歌'])
 for(const ch of text)if(ch.codePointAt(0)>127)assert(dscs.has(ch.codePointAt(0)),ch);
const hangul={LV_FONT_GLYPH_FORMAT_A1:1};vm.createContext(hangul);
vm.runInContext('function describe(cp,out){'+descriptor(korean).replace('unsigned index;','let index;')+'}',hangul);
const kb=Uint8Array.from(array(korean,'bitmap'));assert.equal(kb.length,11266*36);
for(let cp=0xAC00;cp<=0xD7A3;cp++){const d={gid:{}};assert(hangul.describe(cp,d));assert.equal(d.gid.index,(cp-0xAC00+94)*36);}
assert(!hangul.describe(0xD7A4,{gid:{}}));assert(cjk.includes('.fallback=&lv_font_montserrat_16'));assert(korean.includes('.fallback=&m3_font_cjk'));
const convert={lv_draw_buf_width_to_stride:n=>n+4,LV_COLOR_FORMAT_A8:8};vm.createContext(convert);
const convertBody=body(read('main/m3_font_bitmap.c'),'m3_font_bitmap')
 .replace('const uint8_t *source = (const uint8_t *)glyph->resolved_font->dsc + glyph->gid.index;','let source = glyph.resolved_font.dsc.subarray(glyph.gid.index);')
 .replace('uint8_t *target = buffer->data;','let target = buffer.data;')
 .replace(/\b(?:uint32_t|unsigned) /g,'let ').replaceAll('glyph->','glyph.').replaceAll('buffer->','buffer.')
 .replace('target += stride','target = target.subarray(stride)').replaceAll('NULL','null').replace('0x80u','0x80');
vm.runInContext('function convert(glyph,buffer){'+convertBody+'}',convert);
for(const cp of [0x3042,0x30AB,0x97F3,0x20B9F,0x00E9]){
 const glyph={...dscs.get(cp),gid:{...dscs.get(cp).gid},resolved_font:{dsc:bitmap}},stride=glyph.box_w+4;
 const data=new Uint8Array(stride*glyph.box_h).fill(17),buffer={data};assert.equal(convert.convert(glyph,buffer),buffer);
 for(let y=0;y<glyph.box_h;y++)for(let x=0;x<stride;x++){
  const bit=y*glyph.box_w+x;
  assert.equal(data[y*stride+x],x<glyph.box_w?(bitmap[glyph.gid.index+(bit>>3)]&(0x80>>(bit&7))?255:0):17);
 }
}
assert.equal(convert.convert(null,{data:new Uint8Array()}),null);
const utf=read('main/include/m3_utf8.h'),u={};vm.createContext(u);
vm.runInContext('function valid(text){'+body(utf,'m3_utf8_valid')
 .replace('const unsigned char *p = (const unsigned char *)text;','let p = 0;')
 .replace(/\b(?:uint32_t|unsigned) /g,'let ').replaceAll('*p++','text[p++]').replaceAll('*p','text[p]')+'}',u);
const bytes=s=>Buffer.concat([Buffer.from(s),Buffer.from([0])]);
for(const s of ['','ASCII','日本語・ひらがな','韓國 漢字','𠮟る','か\u3099'])assert(u.valid(bytes(s)),s);
for(const bad of [[0x80],[0xc0,0xaf],[0xed,0xa0,0x80],[0xf4,0x90,0x80,0x80],[0xe3,0x81],[0xf5,0x80,0x80,0x80]])assert(!u.valid(bytes(bad)),bad);
const copier={strnlen:(s,n)=>Math.min(s.indexOf(0)<0?s.length:s.indexOf(0),n),memcpy:(out,src,n)=>src.copy(out,0,0,n)};vm.createContext(copier);
vm.runInContext('function copy(out,capacity,text){'+body(utf,'m3_utf8_copy').replace('size_t length','let length').replaceAll('(unsigned char)','')+'}',copier);
for(const text of ['日本語（第①章）𠮟る.mp3','Café 韓国.wav'])for(let capacity=0;capacity<Buffer.byteLength(text)+4;capacity++){
 const out=Buffer.alloc(capacity),n=copier.copy(out,capacity,bytes(text));assert(n<capacity||capacity===0);
 if(capacity){assert.equal(out[n],0);assert(u.valid(out));assert(text.startsWith(out.subarray(0,n).toString('utf8')));}
}
const validator={m3_utf8_valid:s=>u.valid(bytes(s)),strlen:s=>Buffer.byteLength(s),strstr:(s,p)=>s.includes(p),
 strchr:(s,c)=>s.includes(c),strrchr:(s,c)=>s.includes(c)?s.slice(s.lastIndexOf(c)):null,strcasecmp:(a,b)=>a.toLowerCase()===b.toLowerCase()?0:1};vm.createContext(validator);
const validate=body(read('main/sd_media.c'),'sd_media_name_valid')
 .replace('for (const char *p=name;*p;++p)','for (let p=0;p<name.length;p++)')
 .replace('(unsigned char)*p','name.charCodeAt(p)').replace('*p == 127','name.charCodeAt(p) == 127').replaceAll('*p','name[p]')
 .replace('name[0]','name.charAt(0)').replace('size_t length','let length')
 .replace('const char *ext','let ext').replace('const char *types[]={','const types=[').replace(/(\.aac"\s*)\};/,'$1];')
 .replace('sizeof(types)/sizeof(types[0])','types.length').replace('unsigned i','let i');
vm.runInContext('function valid(name){'+validate+'}',validator);
for(const s of ['春の歌.wav','韓國漢字 音樂.mp3','カタカナ（①）.aac','Café £10.wav'])assert(validator.valid(s),s);
for(const s of ['','../song.wav','a/b.wav','a\\b.wav','a?.wav','a\n.wav','a.txt','日本語'.repeat(12)+'.wav'])assert(!validator.valid(s),s);
const config=read('sdkconfig.defaults'),policy=read('cmake/memory_policy.cmake');
assert(config.includes('CONFIG_FATFS_API_ENCODING_UTF_8=y')&&policy.includes('CONFIG_FATFS_API_ENCODING_UTF_8=y'));
assert(policy.includes('CONFIG_FATFS_API_ENCODING_ANSI_OEM'));
const cmake=read('main/CMakeLists.txt');for(const file of ['m3_font_cjk.c','m3_font_bitmap.c'])assert(cmake.includes('"'+file+'"'));
const ui=read('main/m3_ui.c');
assert(ui.includes('*slash = strstr(name, " / ")'),'radio subtitle source pointer remains declared');
assert(ui.includes('m3_utf8_copy(title, sizeof(title), name)'),'radio title truncates only at UTF-8 boundaries');
console.log(`PASS: ${manifest.glyph_count} CJK/kana/symbol glyph descriptors, ${joyo.length} Joyo/variant entries, all Hangul, padded A8 expansion, UTF-8 validation/clipping, SD Unicode names, font chain and saved-config migration.`);

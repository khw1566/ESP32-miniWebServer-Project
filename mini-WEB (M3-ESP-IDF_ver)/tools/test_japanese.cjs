// Actual C language/kana/font/UTF-8 status policies with host mocks; no compiler.
const fs=require('fs'),path=require('path'),vm=require('vm'),assert=require('assert');
const root=path.resolve(__dirname,'..'),read=f=>fs.readFileSync(path.join(root,f),'utf8').replace(/\r\n/g,'\n');
function body(source,name){const m=new RegExp('\\b'+name+'\\([^)]*\\)\\s*\\{').exec(source);assert(m,name);let a=m.index+m[0].length,b=a,d=1;
 while(d){if(source[b]==='{')d++;if(source[b]==='}')d--;b++;}return source.slice(a,b-1);}
const types=s=>s.replace(/\b(?:uint32_t|int32_t|unsigned(?! char)|size_t|int|bool|m3_lang_t|nvs_handle_t) (\w+)/g,'let $1')
 .replace(/\(uint8_t\)|\(uint32_t\)|\(m3_lang_t\)/g,'').replaceAll('NULL','null');
const ja=JSON.parse(read('translations/ja.json')),source=read('main/m3_i18n.c');
const ids=[...read('main/include/m3_i18n.h').matchAll(/\b(?:TEXT|TITLE)\((\w+)\)/g)].map(m=>m[1]);
const lang={language:0,M3_LANG_KO:0,M3_LANG_EN:1,M3_LANG_JA:2,M3_LANG_COUNT:3,T_COUNT:ids.length,
 texts:ids.map(id=>({en:id==='NONE'?'':'en-'+id,ko:id==='NONE'?'':'ko-'+id})),japanese_texts:ids.map(id=>ja.texts[id]),saved:0,commits:0,ESP_OK:0,NVS_READWRITE:1,NVS_READONLY:0,
 nvs_open:()=>0,nvs_close:()=>{},nvs_set_u8:(n,k,v)=>{assert.equal(k,'lang');lang.saved=v;return 0;},nvs_commit:()=>lang.commits++};
vm.createContext(lang);vm.runInContext('function set(next){'+types(body(source,'m3_i18n_set_language')).replace('(unsigned)next','(next>>>0)').replaceAll('&nvs','null')+'}\nfunction tr(id){'+types(body(source,'tr')).replace('(unsigned)id','(id>>>0)')+'}',lang);
for(const code of [0,1,2]){assert(lang.set(code));assert.equal(lang.saved,code);for(let i=0;i<ids.length;i++)assert.equal(lang.tr(i),ids[i]==='NONE'?'':code===2?ja.texts[ids[i]]:(code===0?'ko-':'en-')+ids[i]);}
assert(!lang.set(3));assert(!lang.set(-1));assert.equal(lang.tr(-1),'');assert.equal(lang.tr(ids.length),'');
let load=body(source,'m3_i18n_load').replace('uint8_t saved = 0;','let ref={value:0};').replace(/\bsaved\b/g,'ref.value')
 .replaceAll('&ref.value','ref').replaceAll('&nvs','null');
lang.nvs_get_u8=(n,k,out)=>{out.value=lang.saved;return 0;};vm.runInContext('function load(){'+types(load)+'}',lang);
lang.language=0;lang.saved=2;lang.load();assert.equal(lang.language,2);lang.saved=99;lang.language=1;lang.load();assert.equal(lang.language,1);

const kanaSource=read('main/m3_kana.c'),kana={M3_KANA_VOICED:0,M3_KANA_SEMIVOICED:1,M3_KANA_SMALL:2};vm.createContext(kana);
const kanaBody=body(kanaSource,'m3_kana_transform').replace(/static const uint16_t (\w+)\[\] = \{([^}]+)\};/g,'const $1=[$2];')
 .replace(/sizeof\((\w+)\) \/ sizeof\(\1\[0\]\)/g,'$1.length');
vm.runInContext('function transform(cp,kind){'+types(kanaBody)+'}',kana);
for(const [plain,voiced,semi] of [['は','ば','ぱ'],['ひ','び','ぴ'],['ふ','ぶ','ぷ'],['へ','べ','ぺ'],['ほ','ぼ','ぽ'],['ハ','バ','パ']]){
 assert.equal(String.fromCodePoint(kana.transform(plain.codePointAt(0),0)),voiced);
 assert.equal(String.fromCodePoint(kana.transform(voiced.codePointAt(0),0)),plain);
 assert.equal(String.fromCodePoint(kana.transform(plain.codePointAt(0),1)),semi);
 assert.equal(String.fromCodePoint(kana.transform(semi.codePointAt(0),1)),plain);
}
for(const [plain,small] of [['あ','ぁ'],['つ','っ'],['や','ゃ'],['カ','ヵ'],['ケ','ヶ'],['ア','ァ']]){
 assert.equal(kana.transform(plain.codePointAt(0),2),small.codePointAt(0));assert.equal(kana.transform(small.codePointAt(0),2),plain.codePointAt(0));
}
assert.equal(kana.transform('ウ'.codePointAt(0),0),'ヴ'.codePointAt(0));assert.equal(kana.transform('漢'.codePointAt(0),0),'漢'.codePointAt(0));
kana.strnlen=(s,n)=>Math.min(s.indexOf(0)<0?s.length:s.indexOf(0),n);kana.m3_kana_transform=kana.transform;
vm.runInContext('function edit(text,capacity,kind){'+types(body(kanaSource,'m3_kana_edit'))
 .replace('unsigned char *p = (unsigned char *)text + n - 3;','let p=text.subarray(n-3);')+'}',kana);
for(const [before,after,kind] of [['ABCか','ABCが',0],['は','ぱ',1],['カタカナつ','カタカナっ',2],['ア','ァ',2]]){
 const data=Buffer.alloc(64);data.write(before);assert(kana.edit(data,data.length,kind));assert.equal(data.subarray(0,data.indexOf(0)).toString(),after);
}
for(const before of ['','A','한글','漢字']){const data=Buffer.alloc(64);data.write(before);assert(!kana.edit(data,64,0));}

const region=source.slice(source.indexOf('static const text_pair_t statuses[]'),source.indexOf('#include "m3_i18n_ja.inc"'));
const pairs=[...region.matchAll(/\{"((?:[^"\\]|\\.)*)", "((?:[^"\\]|\\.)*)"\}/g)].map(m=>({en:Buffer.from(JSON.parse('"'+m[1]+'"')),ko:Buffer.from(JSON.parse('"'+m[2]+'"'))}));
const status={language:2,M3_LANG_EN:1,M3_LANG_JA:2,statuses:pairs.slice(0,-3),tokens:pairs.slice(-3),
 japanese_statuses:pairs.map(p=>Buffer.from(ja.statuses[p.en.toString()])),
 strlen:b=>b.indexOf(0)<0?b.length:b.indexOf(0),strnlen:(b,n)=>Math.min(b.indexOf(0)<0?b.length:b.indexOf(0),n),
 strncmp:(a,b,n)=>Buffer.compare(a.subarray(0,n),b.subarray(0,n)),memcpy:(dest,src,n)=>src.copy(dest,0,0,n),
 m3_utf8_copy:(out,capacity,text)=>{let n=Math.min(capacity-1,text.length);while(n&&(text[n]&0xc0)===0x80)n--;text.copy(out,0,0,n);out[n]=0;return n;}};
vm.createContext(status);
let statusBody=types(body(source,'m3_i18n_status')).replace(/const text_pair_t \*(\w+)/g,'let $1').replace(/const char \*(\w+)/g,'let $1')
 .replace(/sizeof\((statuses|tokens)\) \/ sizeof\(\1\[0\]\)/g,'$1.length')
 .replaceAll('&statuses[i]','statuses[i]').replaceAll('&tokens[i]','tokens[i]').replaceAll('best->','best.').replaceAll('token->','token.')
 .replaceAll('best - statuses','statuses.indexOf(best)').replaceAll('token - tokens','tokens.indexOf(token)')
 .replace('english + best_length','english.subarray(best_length)').replaceAll('*rest','rest[0]')
 .replaceAll('buffer + used','buffer.subarray(used)').replace('rest += strlen(token.en)','rest=rest.subarray(strlen(token.en))')
 .replace('rest += bytes','rest=rest.subarray(bytes)').replace('unsigned char lead','let lead').replace('(unsigned char)','');
vm.runInContext('function translate(english,buffer,size){'+statusBody+'}',status);
const decode=b=>new TextDecoder('utf-8',{fatal:true}).decode(b.subarray(0,b.indexOf(0)<0?b.length:b.indexOf(0)));
for(const message of ['Radio stream ended / network timeout','Radio decoding / PCM 80% / net 20% / gaps 3','Recording 春の歌・𠮟る / 8 seconds / level 42']){
 const input=Buffer.concat([Buffer.from(message),Buffer.from([0])]);
 for(let size=1;size<=256;size++){const output=Buffer.alloc(size);assert.equal(status.translate(input,output,size),output);decode(output);}
}
const input=Buffer.from('Radio stream ended / network timeout\0'),output=Buffer.alloc(256);status.translate(input,output,256);
assert.equal(decode(output),ja.statuses['Radio stream ended / network timeout'],'longest matching Japanese status');
status.language=1;assert.equal(status.translate(input,output,256),input);

function array(src,name){return new RegExp('\\b'+name+'\\[\\] = \\{([\\s\\S]*?)\\};').exec(src)[1].split(',').map(x=>x.trim()).filter(Boolean).map(Number);}
function fontBitmap(src){return /\bbitmap_packed\[\] = \{/.test(src)?require('zlib').inflateSync(Buffer.from(array(src,'bitmap_packed'))):array(src,'bitmap');}
const korean=read('main/m3_font_korean.c'),cjk=read('main/m3_font_cjk.c'),large=read('main/m3_font_large.c');
const kb=Uint8Array.from(fontBitmap(korean)),cb=Uint8Array.from(fontBitmap(cjk)),
 cps=[...array(cjk,'codepoints'),...array(cjk,'upper_codepoints')],offsets=array(cjk,'offsets'),index=new Map(cps.map((cp,i)=>[cp,i]));
const smallFonts={korean:{dsc:kb},cjk:{dsc:cb},latin:{}};
function base(cp,out){let i;
 if(cp>=0xAC00&&cp<=0xD7A3)i=cp-0xAC00+94;else if(cp>=0x3131&&cp<=0x318E)i=cp-0x3131;
 if(i!==undefined){Object.assign(out,{box_w:16,box_h:18,adv_w:16,ofs_x:0,ofs_y:-4,gid:{index:i*36},resolved_font:smallFonts.korean,format:1,is_placeholder:false});return true;}
 i=index.get(cp);if(i===undefined)return false;
 const w=(offsets[i+1]-offsets[i])*8;Object.assign(out,{box_w:w,box_h:16,adv_w:cp===0x3099||cp===0x309A?0:w,
  ofs_x:cp===0x3099||cp===0x309A?-w:0,ofs_y:-2,gid:{index:offsets[i]*16},resolved_font:smallFonts.cjk,format:1,is_placeholder:false});return true;
}
const font={base_glyph:(cp,next,out)=>base(cp,out),LV_FONT_GLYPH_FORMAT_A1:1,next:0};vm.createContext(font);
vm.runInContext('function scale(value){return value>=0?Math.floor((value*22+15)/16):-Math.floor((-value*22+15)/16);}\nfunction describe(cp,out){'+types(body(large,'describe'))
 .replace('(void)font;','').replace('lv_font_glyph_dsc_t base;','let base={};').replace('&base','base').replaceAll('out->','out.')+'}',font);
const title=read('main/m3_font_title.c'),height=Number(/\.line_height=(\d+)/.exec(title)[1]),baseline=Number(/\.base_line=(\d+)/.exec(title)[1]);
assert(title.includes('.fallback=&m3_font_large'));assert(large.includes('.fallback=&lv_font_montserrat_20'));
for(const m of title.matchAll(/\.box_w=(\d+), \.box_h=(\d+), \.ofs_x=(-?\d+), \.ofs_y=(-?\d+)/g)){
 const h=+m[2],oy=+m[4];assert(height-baseline-h-oy>=0,'semibold top fits line box');assert(height-baseline-oy<=height,'semibold bottom fits line box');
}
for(const cp of [...cps,...Array.from({length:11172},(_,i)=>i+0xAC00)]){const d={gid:{}};assert(font.describe(cp,d));assert(height-baseline-d.box_h-d.ofs_y>=0);assert(height-baseline-d.ofs_y<=height);}
assert(!font.describe(65,{gid:{}}),'ASCII uses Montserrat 20');
const draw={...font,lv_draw_buf_width_to_stride:w=>(w+3)&~3,LV_COLOR_FORMAT_A8:8};vm.createContext(draw);
let bitmapBody=types(body(large,'bitmap')).replace('lv_font_glyph_dsc_t base;','let base={};').replaceAll('&base','base')
 .replace('const uint8_t *data = m3_font_blob_data(base.resolved_font);\n    if (!data) return null;\n    const uint8_t *source = data + base.gid.index;','let source=base.resolved_font.dsc.subarray(base.gid.index);')
 .replace('uint8_t *target = buffer->data;','let target=buffer.data;').replaceAll('glyph->','glyph.').replaceAll('buffer->','buffer.')
 .replaceAll('y * base.box_h / glyph.box_h','Math.floor(y * base.box_h / glyph.box_h)').replaceAll('x * base.box_w / glyph.box_w','Math.floor(x * base.box_w / glyph.box_w)')
 .replace('0x80u','0x80').replace('target += stride','target=target.subarray(stride)');
vm.runInContext('function bitmap(glyph,buffer){'+bitmapBody+'}',draw);
for(const text of ['큰한글','日本語カレンダー','かなカナ','𠮟'])for(const ch of text){const cp=ch.codePointAt(0),d={gid:{}},b={};assert(font.describe(cp,d));assert(base(cp,b));
 const stride=(d.box_w+3)&~3,buffer={data:new Uint8Array(stride*d.box_h).fill(19)};assert.equal(draw.bitmap(d,buffer),buffer);
 for(let y=0;y<d.box_h;y++)for(let x=0;x<stride;x++){
  const bit=Math.floor(y*b.box_h/d.box_h)*b.box_w+Math.floor(x*b.box_w/d.box_w),src=b.resolved_font.dsc;
  assert.equal(buffer.data[y*stride+x],x<d.box_w?(src[b.gid.index+(bit>>3)]&(0x80>>(bit&7))?255:0):19);
 }
}
const ui=read('main/m3_ui.c');assert(!ui.includes('LV_LABEL_LONG_DOT'));assert(ui.includes('LV_LABEL_LONG_SCROLL_CIRCULAR'));
const label={text:'日本語の長いファイル名',resets:0},stable={strcmp:(a,b)=>a===b?0:1,lv_label_get_text:o=>o.text,lv_label_set_text:(o,t)=>{o.text=t;label.resets++;}};
vm.createContext(stable);vm.runInContext('function set(obj,text){'+body(ui,'set_text')+'}',stable);
for(let i=0;i<60;i++)stable.set(label,label.text);assert.equal(label.resets,0,'metrics refresh preserves marquee position');
stable.set(label,'次のファイル');assert.equal(label.resets,1);
assert(ui.includes('T_LANG_KO, T_LANG_EN, T_LANG_JA'));assert(ui.includes('key_controls[8][64]'));
assert(read('main/CMakeLists.txt').includes('"m3_font_large.c"'));assert(read('main/CMakeLists.txt').includes('"m3_kana.c"'));
console.log('PASS: all Japanese UI selections/NVS restore, kana voiced/semivoiced/small forms, all large Hangul/CJK line bounds, actual scaled A8 pixels, title fallback and circular overflow policies.');

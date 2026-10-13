// Host checks for the Korean/English touch UI strings. No firmware build or display.
const fs=require('fs'),path=require('path'),assert=require('assert');
const root=path.resolve(__dirname,'..'),read=p=>fs.readFileSync(path.join(root,p),'utf8');
const header=read('main/include/m3_i18n.h'),source=read('main/m3_i18n.c'),ui=read('main/m3_ui.c');
const ids=[...header.matchAll(/\b(?:TITLE|TEXT)\((\w+)\)/g)].map(m=>m[1]);
assert.equal(new Set(ids).size,ids.length,'duplicate text IDs');
// C string literal list -> JS strings (adjacent literals are concatenated, \x escapes decoded as UTF-8 bytes).
// Adjacent C literals are concatenated before decoding, so hex escapes may form UTF-8 sequences.
function literals(text){
 const bytes=[];
 for(const m of text.matchAll(/"((?:[^"\\]|\\.)*)"/g))
  for(let i=0,s=m[1];i<s.length;i++){
   if(s[i]!=='\\'){bytes.push(...Buffer.from(s[i],'utf8'));continue;}
   const e=s[++i];
   if(e==='x'){bytes.push(parseInt(s.slice(i+1,i+3),16));i+=2;}
   else bytes.push(e==='n'?10:e.charCodeAt(0));
  }
 return Buffer.from(bytes).toString('utf8');
}
const entries={};
for(const m of source.matchAll(/^    \[T_(\w+)\] = \{([\s\S]*?)\},$/gm)){
 const body=m[2];
 // Split the {en, ko} pair at the first comma outside a string literal.
 let quote=false,cut=-1;
 for(let i=0;i<body.length;i++){const c=body[i];if(c==='\\'){i++;continue;}if(c==='"')quote=!quote;if(!quote&&c===','){cut=i;break;}}
 assert(cut>0,m[1]);
 entries[m[1]]={en:literals(body.slice(0,cut)),ko:literals(body.slice(cut+1))};
}
for(const id of ids){
 assert(id in entries,`missing translation for T_${id}`);
 if(id==='NONE')continue;
 const {en,ko}=entries[id];assert(en&&ko,`empty text for T_${id}`);
 const spec=s=>(s.match(/%[-0-9.]*l?[a-z%]/g)||[]).join(' ');
 assert.equal(spec(ko),spec(en),`format specifiers differ for T_${id}`);
}
assert.equal(Object.keys(entries).length,ids.length,'translations without an ID');
// Fonts contain ASCII, Hangul syllables (+ compatibility jamo) and LVGL symbols only.
const cjk=new Set(JSON.parse(read('third_party/unifont/subset-manifest.json')).codepoints.map(cp=>parseInt(cp,16)));
const allowed=c=>{const p=c.codePointAt(0);return p===10||(p>=32&&p<127)||(p>=0xac00&&p<=0xd7a3)||(p>=0x3131&&p<=0x318e)||(p>=0xf000&&p<=0xf8ff)||cjk.has(p);};
for(const [id,{en,ko}] of Object.entries(entries))for(const c of en+ko)assert(allowed(c),`T_${id} has unsupported glyph U+${c.codePointAt(0).toString(16)}`);
const ja=JSON.parse(read('translations/ja.json'));
assert.deepEqual(Object.keys(ja.texts).sort(),ids.slice().sort(),'all Japanese UI strings exist');
for(const [id,value] of Object.entries(ja.texts)){
 if(id!=='NONE')assert(value,'empty Japanese text '+id);
 for(const c of value)assert(allowed(c),`Japanese T_${id} lacks glyph U+${c.codePointAt(0).toString(16)}`);
 const format=s=>(s.match(/%[-0-9.*]*l?[a-z%]/g)||[]).join(' ');
 assert.equal(format(value),format(entries[id].en),'Japanese format '+id);
}
for(const value of Object.values(ja.statuses))for(const c of value)assert(allowed(c),'Japanese status glyph '+c);
assert(source.includes('#include "m3_i18n_ja.inc"'));
assert(read('main/m3_i18n_ja.inc').includes('[T_LANG_JA] = "日本語"'));
// Every Hangul syllable in a title string is present in the generated title font.
const font=read('main/m3_font_title.c');
const start=parseInt(/range_start=0x([0-9a-f]+)/.exec(font)[1],16);
const glyphs=new Set(/unicode_list\[\] = \{([^}]*)\}/.exec(font)[1].split(',').map(v=>String.fromCodePoint(start+parseInt(v,16))));
for(const id of [...header.matchAll(/\bTITLE\((\w+)\)/g)].map(m=>m[1]))
 for(const c of entries[id].ko)if(/[가-힣]/.test(c))assert(glyphs.has(c),`title font lacks ${c} (run tools/generate_ui_fonts.py)`);
// Status translations must match real service messages (catches drift when a message is reworded).
const services=['m3_runtime.c','m3_audio.c','m3_radio.c','m3_agent.c','m3_voice.c','m3_tts.c','m3_google_audio.c','m3_tube.c','m3_music.c']
 .map(f=>read('main/'+f)).join('\n');
const statuses=[...source.slice(source.indexOf('statuses[] = {'),source.indexOf('tokens[] = {')).matchAll(/\{"((?:[^"\\]|\\.)*)", "((?:[^"\\]|\\.)*)"\}/g)];
assert(statuses.length>100);
// m3_radio.c builds these two from "Radio %s / PCM ..." with "rebuffering"/"decoding".
const composed={'Radio rebuffering / ':'refill ? "rebuffering" : "decoding"','Radio decoding / ':'refill ? "rebuffering" : "decoding"'};
assert(services.includes('"Radio %s / PCM'));
for(const [,en,ko] of statuses){
 assert(services.includes('"'+en)||services.includes(composed[en]),`status not found in services: ${en}`);
 for(const c of ko)assert(allowed(c),`status has unsupported glyph: ${ko}`);
 assert(Buffer.byteLength(ko)<128,`status translation exceeds the 128-byte UI buffer: ${ko}`);
}
// Speech waiting animations key off service status prefixes; each prefix must still be emitted.
const phase=/static unsigned tts_phase[\s\S]*?\n\}/.exec(ui)[0];
const prefixes=[...phase.matchAll(/"([^"]+)"/g)].map(m=>m[1]);
assert.equal(prefixes.length,6);
for(const prefix of prefixes)assert(services.includes('"'+prefix),`tts_phase prefix no longer emitted: ${prefix}`);
// Monitor QR shows the local URL, or the cloud relay address published by the runtime.
assert(read('main/m3_runtime.c').includes('cloud_relay_get_url(next.cloud_url, sizeof(next.cloud_url))'));
assert(ui.includes('const char *url = m3_web_address_url(&snapshot, web_address_kind);'));
assert(ui.includes('lv_qrcode_update(web_qr, url, strlen(url))'));
// Every UI text ID referenced by m3_ui.c exists, and the UI never hardcodes English captions in labels.
for(const id of new Set([...ui.matchAll(/\bT_([A-Z0-9_]+)\b/g)].map(m=>m[1])))if(id!=='COUNT')assert(ids.includes(id),`unknown T_${id}`);
assert(!/label\([^,]+, "[A-Za-z][a-z]+ [a-z]+/.test(ui),'user-visible English sentence bypasses translation');
assert(source.includes('nvs_open("m3_ui", NVS_READWRITE'),'language choice persists');
// Icon-prefixed button captions are composed in a 128-byte buffer; other bound text is set directly.
const prefixed=[...ui.matchAll(/button\([^,]+, LV_SYMBOL_\w+, T_(\w+)/g)].map(m=>m[1]);
for(const id of prefixed)assert(Buffer.byteLength(entries[id].ko)<110&&Buffer.byteLength(entries[id].en)<110,`button caption too long: T_${id}`);
for(const id of prefixed)assert(Buffer.byteLength(ja.texts[id])<110,`Japanese button caption too long: T_${id}`);
assert(!/char text\[160\];\s*const char \*value = tr/.test(ui),'bound help text must not pass through a small buffer');
console.log(`PASS: ${ids.length} KO/EN/JA texts with matching formats and glyph coverage, title-font coverage, ${statuses.length} service translations, Japanese labels/statuses and persisted language.`);

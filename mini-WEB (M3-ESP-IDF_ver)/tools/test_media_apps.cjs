// Execute platform-independent C policies through a narrow source translation.
// Does not compile firmware, emulate I2S, or establish a hardware connection.
const fs=require('fs'),path=require('path'),assert=require('assert'),vm=require('vm'),crypto=require('crypto');
const root=path.resolve(__dirname,'..'),read=p=>fs.readFileSync(path.join(root,p),'utf8');
function translate(source){
 return source.replace(/^#.*$/gm,'').replace(/\/\*[\s\S]*?\*\//g,'')
  .replace(/(?:static )?(?:uint32_t|unsigned|bool|void|int) (\w+)\(([^)]*)\)\s*\{/g,(_,name,args)=>
   'function '+name+'('+args.split(',').map(a=>a.trim().split(/[ *]+/).at(-1)).join(',')+'){')
  .replace(/\bgame->/g,'game.').replace(/\(const unsigned char \*\)|\(unsigned char\)|\(unsigned\)|\(int\)|\(char\)|\(uint32_t\)/g,'')
  .replace(/\b(?:uint8_t|uint32_t|unsigned|bool|size_t|int) (\w+)\[(\d+)\] = \{0\}/g,(_,name,n)=>'let '+name+' = new Uint8Array('+n+')')
  .replace(/(\w+)\[(\d+)\] = \{0\}/g,'$1 = new Uint8Array($2)')
  .replace(/\b(?:uint8_t|uint32_t) (\w+)\[\d+\] = \{([^}]+)\}/g,'let $1 = [$2]')
  .replace(/\b(?:char|unsigned) (\w+)\[(\d+)\]/g,'let $1 = new Uint8Array($2)')
  .replace(/\b(?:uint8_t|uint32_t|unsigned|bool|size_t|int) (\w+)/g,'let $1')
  .replace(/\bNULL\b/g,'null').replace(/UINT32_C\(([^)]+)\)/g,'$1');
}
let source=read('main/m3_2048.c');
source=translate(source).replace('game.random = x; return x','game.random = x >>> 0; return x >>> 0')
 .replace('x >> 17','x >>> 17').replace('memset(game, 0, sizeof(*game))','reset(game)');
const engine={M3_GAME_LEFT:0,M3_GAME_RIGHT:1,M3_GAME_UP:2,M3_GAME_DOWN:3,UINT32_MAX:0xffffffff,
 reset:g=>Object.assign(g,{tiles:new Uint8Array(16),score:0,random:0,won:false})};
vm.createContext(engine);vm.runInContext(source,engine);
function game(row){return {tiles:Uint8Array.from([...row,...Array(16-row.length).fill(0)]),score:0,random:17,won:false};}
let g=game([1,1,1,1]);assert(engine.m3_2048_slide(g,0));assert.deepEqual([...g.tiles.slice(0,4)],[2,2,0,0]);assert.equal(g.score,8);
g=game([1,1,2,0]);engine.m3_2048_slide(g,0);assert.deepEqual([...g.tiles.slice(0,4)],[2,2,0,0],'merged tile must not merge twice');
g=game([1,1,2,0]);engine.m3_2048_slide(g,1);assert.deepEqual([...g.tiles.slice(0,4)],[0,0,2,2]);
g=game([1,0,0,0,1]);engine.m3_2048_slide(g,2);assert.equal(g.tiles[0],2);assert.equal(g.tiles[4],0);
g=game([1,0,0,0,1]);engine.m3_2048_slide(g,3);assert.equal(g.tiles[12],2);assert.equal(g.tiles[0],0);
g=game([10,10]);engine.m3_2048_slide(g,0);assert(g.won);assert.equal(g.tiles[0],11);assert.equal(g.score,2048);
g=game([1]);const before=[...g.tiles];assert(!engine.m3_2048_move(g,0));assert.deepEqual([...g.tiles],before,'invalid move must not spawn');
g=game([1]);assert(engine.m3_2048_move(g,1));assert.equal([...g.tiles].filter(Boolean).length,2);
g=game([1,2,1,2,2,1,2,1,1,2,1,2,2,1,2,1]);assert(!engine.m3_2048_can_move(g));
g.tiles[0]=2;assert(engine.m3_2048_can_move(g));
for(let seed=0;seed<100;seed++){g={};engine.m3_2048_reset(g,seed);assert.equal([...g.tiles].filter(Boolean).length,2);assert([...g.tiles].every(n=>n<=2));}

// Convert only the small Hangul input engine; UTF-8 buffers retain C semantics.
source=read('main/m3_hangul.c').replace(/static const uint(?:8|16)_t (\w+)\[[^;=]+ = (\{[^;]+\});/g,
 (_,name,literal)=>'const '+name+' = '+literal.replace(/\{/g,'[').replace(/\}/g,']')+';');
source=translate(source).replace(/\*p\b/g,'p[0]')
 .replace('syllable / 588','Math.floor(syllable / 588)').replace('syllable / 28 % 21','Math.floor(syllable / 28) % 21')
 .replaceAll('sizeof(vowel_pairs)/sizeof(vowel_pairs[0])','vowel_pairs.length')
 .replaceAll('sizeof(final_pairs)/sizeof(final_pairs[0])','final_pairs.length')
 .replace('decode(text + start)','decode(text.subarray(start))')
 .replace('encode(replacement[i], suffix + bytes)','encode(replacement[i], suffix.subarray(bytes))')
 .replace('memcpy(text + start, suffix, bytes)','memcpy(text.subarray(start), suffix, bytes)');
const hangul={strlen:b=>{const i=b.indexOf(0);return i<0?b.length:i;},
 memchr:(b,value,cap)=>b.subarray(0,cap).includes(value),memcpy:(a,b,n)=>a.set(b.subarray(0,n))};
vm.createContext(hangul);vm.runInContext(source,hangul);
function compose(keys,cap=385){const text=new Uint8Array(cap);for(const key of keys){const bytes=Buffer.concat([Buffer.from(key),Buffer.from([0])]);assert(hangul.m3_hangul_append(text,cap,bytes),key);}return Buffer.from(text.subarray(0,text.indexOf(0))).toString();}
assert.equal(compose('ㅇㅏㄴㄴㅕㅇㅎㅏㅅㅔㅇㅛ'),'안녕하세요');
assert.equal(compose('ㄷㅐㅎㅏㄴㅁㅣㄴㄱㅜㄱ'),'대한민국');
assert.equal(compose('ㄱㅗㅏㄴ'),'관');assert.equal(compose('ㄱㅏㄱㅏ'),'가가');
assert.equal(compose('ㄱㅏㅂㅅㅏ'),'갑사');assert.equal(compose('ㄱㅏㅂㅅㅇㅣ'),'값이');
assert.equal(compose('ㄲㅗㅊ'),'꽃');assert.equal(compose('ㅎㅏㄴ ㄱㅡㄹ'),'한 글');
const full=Uint8Array.from([97,98,0]);assert(!hangul.m3_hangul_append(full,3,Buffer.from('ㄱ\0')));assert.deepEqual([...full],[97,98,0]);

// ICY metadata stripping, including every possible read boundary.
const radio=read('main/m3_radio.c');const match=/static unsigned icy_filter\([^)]*\) \{([\s\S]*?)\n\}/.exec(radio);assert(match);
let filter=match[1].replace(/icy->/g,'icy.').replace(/\bunsigned /g,'let ').replace(/\(unsigned\)/g,'');
const icyContext={};vm.createContext(icyContext);vm.runInContext('function filter(icy,buffer,size){'+filter+'}',icyContext);
const audio=Buffer.from('abcdefghijkl'),wire=Buffer.concat([audio.subarray(0,4),Buffer.from([1]),Buffer.alloc(16,88),audio.subarray(4,8),Buffer.from([0]),audio.subarray(8),Buffer.from([0])]);
for(let chunk=1;chunk<=wire.length;chunk++){
 const icy={interval:4,audio_left:4,metadata_left:0,length_next:false},result=[];
 for(let start=0;start<wire.length;start+=chunk){const b=Buffer.from(wire.subarray(start,start+chunk));result.push(b.subarray(0,icyContext.filter(icy,b,b.length)));}
 assert.equal(Buffer.concat(result).toString(),audio.toString());
}
const manifest=JSON.parse(read('tts_data/manifest.json'));
for(const [name,info] of Object.entries(manifest)){
 const bytes=fs.readFileSync(path.join(root,'tts_data/espeak-ng-data',name));assert.equal(bytes.length,info.size);
 assert.equal(crypto.createHash('sha256').update(bytes).digest('hex'),info.sha256);
}
const phondata=fs.readFileSync(path.join(root,'tts_data/espeak-ng-data/phondata'));
assert.equal(phondata.readUInt32LE(0),0x014801);assert.equal(phondata.readUInt32LE(4),22050);
assert(read('tts_data/espeak-ng-data/lang/ko').includes('language ko'));
const tts=read('main/m3_tts.c');assert(tts.includes('espeak_SetVoiceByName("ko")'));assert(tts.includes('ENOUTPUT_MODE_SYNCHRONOUS'));
assert(!/esp_http|network_|fopen\(/.test(tts),'TTS must synthesize without network/SD');
const validator=/bool m3_speech_text_valid\([^)]*\) \{([\s\S]*?)\n\}/.exec(tts);assert(validator);
source=validator[1].replace('!*text','!text[0]')
 .replace('const unsigned char *p = (const unsigned char *)text;','const bytes = text; let p = 0;')
 .replace(/\*p\+\+/g,'bytes[p++]').replace(/\*p/g,'bytes[p]')
 .replace(/\b(?:unsigned|bool) (\w+)/g,'let $1');
vm.runInContext('function valid(text){'+source+'}',hangul);
function textBytes(text){return Buffer.concat([Buffer.from(text),Buffer.from([0])]);}
assert(hangul.valid(textBytes('안녕하세요. 123')));assert(!hangul.valid(textBytes(' \n\t')));
assert(!hangul.valid(Buffer.from([0xe3,0x80,0])));assert(!hangul.valid(Buffer.from([0xc0,0x80,0])));
assert(!hangul.valid(Buffer.from([0xed,0xa0,0x80,0])));assert(!hangul.valid(textBytes('가'.repeat(129))));
assert(hangul.valid(textBytes('가'.repeat(128))));
const pcmCallback=/static int speech_pcm\([^)]*\) \{([\s\S]*?)\n\}/.exec(tts);assert(pcmCallback);
source=pcmCallback[1].replace('(void)events;','').replace('int16_t stereo[480 * 2];','let stereo = new Int16Array(960);')
 .replace(/sink->/g,'sink.').replace(/\bint (\w+)/g,'let $1').replace(/\(unsigned\)/g,'');
const speech={write_failed:false,cancelled:false,fail:false,writes:[],taskYIELD:()=>{},
 sink:{cancelled:()=>speech.cancelled,write:(pcm,n)=>{speech.writes.push([...pcm.slice(0,n/2)]);return !speech.fail;}}};
vm.createContext(speech);vm.runInContext('function callback(samples,count,events){'+source+'}',speech);
const mono=Int16Array.from({length:1001},(_,i)=>i-500);assert.equal(speech.callback(mono,mono.length,null),0);
assert.deepEqual(speech.writes.map(a=>a.length),[960,960,82]);
assert.deepEqual(speech.writes.flat(),[...mono].flatMap(n=>[n,n]));
speech.cancelled=true;assert.equal(speech.callback(mono,mono.length,null),1);speech.cancelled=false;
speech.fail=true;assert.equal(speech.callback(mono,mono.length,null),1);assert(speech.write_failed);
assert.equal(speech.callback(null,0,null),0);
assert(read('main/m3_font_korean.c').includes('cp <= 0xd7a3'));
assert(read('main/m3_font_korean.c').includes('.fallback=&m3_font_cjk'));
assert(radio.includes('https_memory_exclusive_end()'));assert(radio.includes('.timeout_ms = 3000'));
assert(read('m3.ps1').includes("'components'"));
console.log('PASS: actual 2048 movement/merge/score/win/game-over/spawn policies, Korean syllable/final/vowel composition, ICY metadata at all read boundaries, voice-data hashes/format, offline TTS and staging contracts.');

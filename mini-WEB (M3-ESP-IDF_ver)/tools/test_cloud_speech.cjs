// Host execution of RIFF parsing and the PCM playback loop. No API or hardware.
const fs=require('fs'),path=require('path'),vm=require('vm'),assert=require('assert');
const root=path.resolve(__dirname,'..'),read=p=>fs.readFileSync(path.join(root,p),'utf8').replace(/\r\n/g,'\n');
const source=read('main/m3_google_audio.c'),voice=read('main/m3_voice.c'),audio=read('main/m3_audio.c');
function body(name){const m=new RegExp('\\b'+name+'\\([^)]*\\)\\s*\\{').exec(source);assert(m,name);
 let start=m.index+m[0].length,end=start,depth=1;while(depth){if(source[end]==='{')depth++;if(source[end]==='}')depth--;end++;}return source.slice(start,end-1);}
const context={memcmp:(p,s,n)=>Buffer.compare(p.subarray(0,n),Buffer.from(s).subarray(0,n))};
vm.createContext(context);
vm.runInContext('function read16(p){'+body('read16').replace('(unsigned)','')+'}\n'+
 'function read32(p){return (read16(p) | read16(p.subarray(2)) << 16) >>> 0;}\n'+
 'function parse(p,size,result){'+body('wav_pcm').replace(/\b(?:size_t|bool) (\w+)/g,'let $1')
 .replace(/\(size_t\)/g,'').replace(/\*(offset|bytes|channels|rate)/g,'result.$1')
 .replace(/p \+ (pos \+ 4|start \+ (?:14|12|8|4|2)|start|pos|8|4)/g,'p.subarray($1)')+'}',context);
function chunk(id,data){const b=Buffer.alloc(8+data.length+(data.length&1));b.write(id);b.writeUInt32LE(data.length,4);data.copy(b,8);return b;}
function fixture(channels=1,rate=24000,extra=true){const fmt=Buffer.alloc(16);fmt.writeUInt16LE(1);fmt.writeUInt16LE(channels,2);
 fmt.writeUInt32LE(rate,4);fmt.writeUInt32LE(rate*channels*2,8);fmt.writeUInt16LE(channels*2,12);fmt.writeUInt16LE(16,14);
 const chunks=[chunk('fmt ',fmt)];if(extra)chunks.push(chunk('JUNK',Buffer.from('odd')));chunks.push(chunk('data',Buffer.alloc(480*channels*2)));
 const p=Buffer.concat([Buffer.from('RIFF0000WAVE'),...chunks]);p.writeUInt32LE(p.length-8,4);return p;}
for(const channels of [1,2])for(const rate of [8000,24000,48000]){
 const p=fixture(channels,rate),r={};assert(context.parse(p,p.length,r));assert.equal(r.channels,channels);assert.equal(r.rate,rate);
 assert.equal(r.bytes,480*channels*2);assert.equal(p.toString('ascii',r.offset-8,r.offset-4),'data');
 for(const n of [0,11,20,p.length-1])assert(!context.parse(p,n,{}));
}
for(const [offset,value,width] of [[4,0xffffffff,4],[20,3,2],[22,3,2],[24,96000,4],[28,0,4],[32,1,2],[34,8,2],[16,0xffffffff,4]]){
 const p=fixture();width===4?p.writeUInt32LE(value,offset):p.writeUInt16LE(value,offset);assert(!context.parse(p,p.length,{}));
}
const loop=body('m3_openai_tts_run').split('int16_t stereo[960];')[1].split('\ndone:')[0];
const playback={read16:context.read16,sign16:v=>v&0x8000?v-65536:v};vm.createContext(playback);
vm.runInContext('function play(pcm,bytes,output){let stereo = new Int16Array(960), ok, error;'+loop
 .replace(/\bsize_t (\w+)/g,'let $1').replaceAll('output->','output.')
 .replace('(int16_t)read16(pcm + offset + i * 2)','sign16(read16(pcm.subarray(offset + i * 2)))')+
 'return {ok,error};}',playback);
const pcm=Buffer.alloc(1502*2);for(let i=0;i<1502;i++)pcm.writeInt16LE(i%2?-12345:23456,i*2);
function run(stopAfter=Infinity,failAfter=Infinity){let written=[],drains=0;
 const output={cancelled:()=>written.length>=stopAfter,write:(samples,bytes)=>{if(written.length>=failAfter)return false;written.push(Array.from(samples.subarray(0,bytes/2)));return true;},
 drain:()=>{drains++;return true;},message:()=>{}};
 return {result:playback.play(pcm,pcm.length,output),written,drains};}
const success=run();assert(success.result.ok);assert.equal(success.drains,1);assert.equal(success.written.length,4);
const samples=success.written.flat();assert.equal(samples.length,1502*2);
for(let i=0;i<1502;i++)assert.deepEqual(samples.slice(i*2,i*2+2),[pcm.readInt16LE(i*2),pcm.readInt16LE(i*2)]);
for(const r of [run(1),run(Infinity,1)]){assert(!r.result.ok);assert.equal(r.drains,0);assert.equal(r.written.length,1);}
assert(source.includes('"model", "gpt-4o-mini-tts"'));assert(source.includes('"voice", "marin"'));
assert(source.includes('"response_format", "pcm"'));assert(source.includes('output->begin(24000)'));
assert(voice.includes('gpt-4o-mini-transcribe\\r\\n'));assert(voice.includes('language\\"\\r\\n\\r\\nko\\r\\n'));
assert(source.includes('"inlineData"'));assert(source.includes('"mimeType", "audio/wav"'));
assert(source.includes('"ko-KR-Chirp3-HD-Aoede"'));assert(source.includes('.disable_auto_redirect = true'));
assert(source.includes('https_memory_exclusive_end()'));assert(source.includes('TTS_RESPONSE_LIMIT = 2 * 1024 * 1024'));
assert(audio.includes('PLAY_OPENAI_TTS'));assert(audio.includes('m3_audio_tts_provider(unsigned provider)'));
// Segment index = provider number passed to m3_voice_set_provider / m3_audio_tts_provider.
const ui=read('main/m3_ui.c');assert(ui.includes('recognizers[] = {"OpenAI", "Gemini 3.1", "Gemini 3.5", "Azure"}'));
assert(ui.includes('engines[] = {T_TTS_OPENAI, T_TTS_GOOGLE, T_TTS_OFFLINE, T_TTS_AZURE}'));
assert(ui.includes('m3_voice_set_provider(segment_value(voice_provider))'));assert(ui.includes('m3_audio_tts_provider(provider)'));
const agent=read('main/m3_agent.c');assert(agent.indexOf('if (leased) https_memory_exclusive_end();')<agent.indexOf('if (!m3_audio_speak(deferred_speech, 165))'));
assert(!/API_KEY\s+"[^"\n]+"/.test(read('main/include/m3_gemini_private.example.h')),'public template contains no keys');
console.log('PASS: RIFF chunks/bounds/PCM validation, OpenAI mono24k signed PCM stereo playback across blocks, stop/write failure/drain, provider/model routes, TLS release before agent speech, empty public key template.');

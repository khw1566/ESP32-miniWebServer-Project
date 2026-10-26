// Execute key/region validation, XML escaping and PCM conversion from the actual C bodies.
// Host mocks only; no firmware compiler, keys, microphone or cloud calls.
const fs = require('fs'), path = require('path'), vm = require('vm'), assert = require('assert');
const root = path.resolve(__dirname, '..');
const read = p => fs.readFileSync(path.join(root, p), 'utf8').replace(/\r\n/g, '\n');
const source = read('main/m4_azure_audio.c');
function body(name) {
  const match = new RegExp('\\b' + name + '\\([^;]*?\\)\\s*\\{').exec(source);
  assert(match, name);
  let start = match.index + match[0].length, end = start, depth = 1;
  while (depth) { if (source[end] === '{') depth++; if (source[end] === '}') depth--; end++; }
  return source.slice(start, end - 1);
}
const validation = {};
vm.createContext(validation);
vm.runInContext('function ready(keyValue,regionValue){' + body('m4_azure_audio_ready')
  .replace('const char *key = m4_sd_azure_key(), *region = m4_sd_azure_region();', 'const key=keyValue, region=regionValue;')
  .replace('size_t length = strlen(key), region_length = strlen(region);', 'const length=key.length, region_length=region.length;')
  .replace(/size_t i/g, 'let i').replaceAll('(unsigned char)key[i]', 'key.charCodeAt(i)') + '}', validation);
const fake = 'fixture-key-'.repeat(4);
assert(validation.ready(fake, 'koreacentral'));
for (const key of ['', 'short', 'x'.repeat(256), fake + '\r\nInjected: yes', fake + ' ']) assert(!validation.ready(key, 'koreacentral'));
for (const region of ['', 'KoreaCentral', '../evil', 'koreacentral.evil', 'x'.repeat(32), 'https://evil']) assert(!validation.ready(fake, region));

const xml = { Buffer, strlen: value => value.length };
vm.createContext(xml);
let xmlBody = body('xml_text')
  .replace('size_t used = 0;', 'let used = 0;')
  .replace('for (const unsigned char *p = (const unsigned char *)text; *p; ++p)', 'for (let n=0,p=text[0]; n<text.length; p=text[++n])')
  .replace('const char *escape', 'const escape').replace('size_t count', 'const count')
  .replaceAll('(char)*p', 'p').replaceAll('*p', 'p').replaceAll('NULL', 'null')
  .replace('memcpy(out + used, escape, count)', 'Buffer.from(escape).copy(out, used, 0, count)');
xmlBody = xmlBody.replace(/'(?:\\.|[^'\\])'/g, value => {
  const decoded = value === "'\\''" ? "'" : value.slice(1, -1);
  return String(decoded.charCodeAt(0));
});
vm.runInContext('function escape(text,out,capacity){' + xmlBody + '}', xml);
const text = '한국어 & <voice> "테스트" \' 태그';
const expected = '한국어 &amp; &lt;voice&gt; &quot;테스트&quot; &apos; 태그';
let escaped = Buffer.alloc(2305);
assert(xml.escape(Buffer.from(text), escaped, escaped.length));
assert.equal(escaped.subarray(0, escaped.indexOf(0)).toString('utf8'), expected);
assert(!xml.escape(Buffer.from('&'), Buffer.alloc(5), 5));
assert(xml.escape(Buffer.from('&'), Buffer.alloc(6), 6));

// The actual conversion block must carry an odd byte across HTTP reads.
let pcmBody = source.slice(source.indexOf('        int16_t stereo[1024];'), source.indexOf('    if (used || !started)'));
pcmBody = pcmBody.slice(0, pcmBody.lastIndexOf('    }'));
pcmBody = pcmBody.replace('int16_t stereo[1024];', 'let stereo=new Int16Array(1024);')
  .replace('size_t frames = used / 2;', 'const frames=Math.floor(used/2);')
  .replace(/size_t i/g, 'let i').replaceAll('(int16_t)', '').replaceAll('(unsigned)', '')
  .replaceAll('out->', 'out.').replace(/error = "[^"]*"; goto done;/g, 'return false;');
const pcm = {}; vm.createContext(pcm);
vm.runInContext('function convert(input,used,out){' + pcmBody + 'return used;}', pcm);
const wire = Buffer.alloc(3002); for (let i=0;i<1501;i++) wire.writeInt16LE(i%2 ? -32000 : 12345, i*2);
for (const chunk of [1,2,3,127,511,1024]) {
  const input=Buffer.alloc(1025), samples=[]; let used=0;
  const out={write:(s,n)=>{samples.push(...s.subarray(0,n/2));return true;}};
  for (let offset=0;offset<wire.length;offset+=chunk) {
    const count=Math.min(chunk,wire.length-offset); wire.copy(input,used,offset,offset+count); used+=count;
    if (used>=2) used=pcm.convert(input,used,out);
  }
  assert.equal(used,0); assert.equal(samples.length,3002);
  for (let i=0;i<1501;i++) assert.deepEqual(samples.slice(i*2,i*2+2),[wire.readInt16LE(i*2),wire.readInt16LE(i*2)]);
}
assert.equal(pcm.convert(Buffer.alloc(2),2,{write:()=>false}),false);
assert(source.includes('.disable_auto_redirect = true'));
assert(source.includes('Ocp-Apim-Subscription-Key'));
assert(source.includes('raw-16khz-16bit-mono-pcm'));
assert(source.includes('language=ko-KR&format=simple'));
assert(source.includes('AZURE_PCM_LIMIT'));
assert(source.includes('https_memory_exclusive_end()'));
assert(source.indexOf('out->begin(AZURE_RATE)') < source.indexOf('if (used || !started)'), 'playback begins before complete response');
assert(read('main/m4_voice.c').includes('if (provider == 3)'));
assert(read('main/m4_audio.c').includes('PLAY_AZURE_TTS'));
assert(read('main/CMakeLists.txt').includes('"m4_azure_audio.c"'));
const timing = { now: 100, esp_timer_get_time: () => timing.now }; vm.createContext(timing);
vm.runInContext('function alive(cancelled,context,deadline){' + body('alive') + '}', timing);
assert(timing.alive(() => false, null, 101));
assert(!timing.alive(() => true, null, 101));
assert(!timing.alive(() => false, null, 100));
const ui = read('main/m4_ui.c');
let phase = /static unsigned tts_phase[^]*?\n\}/.exec(ui)[0];
phase = phase.slice(phase.indexOf('{') + 1, phase.lastIndexOf('}'))
  .replace(/static const char \*const (\w+)\[\] = \{([^]*?)\};/g, 'const $1 = [$2];')
  .replace('sizeof(preparing) / sizeof(preparing[0])', 'preparing.length')
  .replace('unsigned i', 'let i').replaceAll('audio->', 'audio.');
const phases = { strlen: s => s.length, strncmp: (a,b,n) => a.slice(0,n) === b.slice(0,n) ? 0 : 1 };
vm.createContext(phases); vm.runInContext('function phase(audio){' + phase + '}', phases);
assert.equal(phases.phase({playing:true,message:'Azure TTS / preparing Korean audio...'}), 1);
assert.equal(phases.phase({playing:true,message:'Azure TTS / speaking Korean'}), 2);
assert.equal(phases.phase({playing:false,message:'Azure TTS / speaking Korean'}), 0);
// Prevent copied runtime fields from breaking source-level preview mocks.
const simulator = read('tools/ui_sim/sim_main.c');
for (const name of ['audio','voice','agent']) {
  const header=read('main/include/m4_'+name+'.h');
  const definition=/typedef struct \{([^]*?)\} m4_\w+_state_t;/.exec(header)[1];
  const mock=new RegExp('void m4_'+name+'_state\\([^]*?\\n\\}').exec(simulator)[0];
  for (const field of new Set([...mock.matchAll(/out->(\w+)/g)].map(m=>m[1])))
    assert(new RegExp('\\b'+field+'\\b').test(definition), name+' mock has undeclared field '+field);
}
console.log('PASS: actual C key/region validation, SSML escaping, signed PCM conversion at odd/even HTTP boundaries, write failure, bounded TLS/HTTP and provider registration. Host mocks only.');

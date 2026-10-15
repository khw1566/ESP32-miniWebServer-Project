// Execute the actual selection and DSP C bodies without invoking a compiler.
const fs = require('fs'), path = require('path'), vm = require('vm'), assert = require('assert');
let source = fs.readFileSync(path.join(__dirname, '../main/m3_music.c'), 'utf8');
function body(name) {
  const match = new RegExp('\\b' + name + '\\([^)]*\\)\\s*\\{').exec(source);
  assert(match, name); let start = match.index + match[0].length, end = start, depth = 1;
  while (depth) { if (source[end] === '{') depth++; if (source[end] === '}') depth--; end++; }
  return source.slice(start, end - 1);
}
const context = {
  M3_MUSIC_REPEAT_OFF: 0, M3_MUSIC_REPEAT_ALL: 1, M3_MUSIC_REPEAT_ONE: 2,
  state: {eq_db: [0, 0, 0], bands: [0, 0, 0]},
  powf: Math.pow, expf: Math.exp, fabsf: Math.abs,
};
vm.createContext(context);
vm.runInContext('function target(count,selected,direction,automatic,mode,shuffle,random,played=[]){' + body('music_target').replace(/\bunsigned (\w+)/g, 'let $1') + '}', context);
const target = context.target;
assert.equal(target(0, 0, 1, true, 0, false, 0), 0);
assert.equal(target(3, 2, 1, true, 0, false, 0), 3, 'end stops without repeat');
assert.equal(target(3, 2, 1, true, 1, false, 0), 0, 'repeat all wraps');
assert.equal(target(3, 1, 1, true, 2, true, 0), 1, 'repeat one takes priority over shuffle');
assert.equal(target(3, 1, 1, false, 2, false, 0), 2, 'manual next overrides repeat one');
assert.equal(target(3, 0, -1, false, 2, false, 0), 2);
assert.equal(target(3, 2, 1, true, 0, true, 0, [true,true,true]), 3, 'shuffle pass stops without repeat');
assert.equal(target(3, 1, 1, true, 0, true, 0, [true,true,false]), 2, 'shuffle selects an unplayed track');
assert.notEqual(target(3, 2, 1, true, 1, true, 0, [true,true,true]), 2, 'repeat all starts another shuffle pass');
for (let count = 2; count <= 64; count++) for (let selected = 0; selected < count; selected++) {
  const choices = new Set();
  for (let random = 0; random < count - 1; random++) {
    const next = target(count, selected, 1, true, 0, true, random);
    assert(next >= 0 && next < count && next !== selected); choices.add(next);
  }
  assert.equal(choices.size, count - 1);
}
let process = body('music_process')
  .replace(/port(?:ENTER|EXIT)_CRITICAL\(&lock\);/g, '')
  .replace('int db[3]; float energy[3] = {0}, gain[3];', 'let db = [], energy = [0,0,0], gain = [];')
  .replace('memcpy(db, state.eq_db, sizeof(db))', 'db = [...state.eq_db]')
  .replace('float bands[3] = {filter->low[ch], filter->upper[ch] - filter->low[ch], x - filter->upper[ch]};',
           'let bands = [filter.low[ch], filter.upper[ch] - filter.low[ch], x - filter.upper[ch]];')
  .replace(/\b(?:unsigned|float|bool) (\w+)/g, 'let $1')
  .replaceAll('filter->', 'filter.').replace('(int16_t)y', 'Math.trunc(y)')
  .replace(/(\d+(?:\.\d+)?)f\b/g, '$1');
// C conversion to unsigned truncates the nonnegative meter value.
process = process.replace('state.bands[b] = level > 100 ? 100 : level;', 'state.bands[b] = Math.trunc(level > 100 ? 100 : level);');
vm.runInContext('function process(pcm,samples,channels,rate,filter){' + process + '}', context);
function dsp(pcm, channels, rate, filter, eq = [0,0,0]) {
  context.state.eq_db = eq;
  context.process(pcm, pcm.length, channels, rate, filter);
  return [...context.state.bands];
}
const history = () => ({low: [0,0], upper: [0,0]});
for (const channels of [1, 2]) for (const rate of [8000, 24000, 44100, 48000]) {
  const original = Int16Array.from({length: 4096}, (_, i) => i % 2 ? -32768 : 32767);
  const pcm = original.slice(); dsp(pcm, channels, rate, history()); assert.deepEqual(pcm, original, 'flat EQ is bit exact');
}
for (const [frequency, band] of [[60, 0], [1000, 1], [10000, 2]]) {
  const pcm = Int16Array.from({length: 48000}, (_, i) => Math.sin(i * 2 * Math.PI * frequency / 48000) * 8000);
  const levels = dsp(pcm, 1, 48000, history());
  assert(levels[band] > levels[(band + 1) % 3] && levels[band] > levels[(band + 2) % 3], `${frequency} Hz: ${levels}`);
}
const loud = Int16Array.from({length: 1000}, (_, i) => i < 500 ? 30000 : -30000);
dsp(loud, 1, 48000, history(), [12,12,12]);
assert.equal(loud[499], 32767); assert.equal(loud[999], -32768);
const stereo = Int16Array.from({length: 4000}, (_, i) => i % 2 ? 0 : 16000);
dsp(stereo, 2, 48000, history(), [6,-3,2]);
assert(stereo.every((sample, i) => !(i % 2) || sample === 0), 'channels do not leak');
// Split decoder frames retain history and produce identical EQ output.
const full = Int16Array.from({length: 4096}, (_, i) => Math.sin(i / 7) * 12000), split = full.slice();
dsp(full, 2, 44100, history(), [6,-3,2]);
const filter = history(); dsp(split.subarray(0,2048), 2, 44100, filter, [6,-3,2]); dsp(split.subarray(2048), 2, 44100, filter, [6,-3,2]);
assert.deepEqual(split, full);
source = fs.readFileSync(path.join(__dirname, '../main/m3_ui.c'), 'utf8');
const swipe = {LV_DIR_LEFT:1,LV_DIR_RIGHT:2,T_MUSIC_FAIL:3,music_info:{},steps:[],failed:0,
  lv_event_get_indev:event=>event.input,lv_indev_get_gesture_dir:input=>input.dir,
  m3_music_next:direction=>{swipe.steps.push(direction);return swipe.accept;},accept:true,
  tr:id=>id,set_text:()=>swipe.failed++};
vm.createContext(swipe);
vm.runInContext('function event(event){'+body('music_swipe_event')
  .replace('lv_indev_t *input','let input').replace('lv_dir_t dir','let dir')+'}',swipe);
swipe.event({input:{dir:1}});swipe.event({input:{dir:2}});
swipe.event({input:{dir:4}});swipe.event({input:null});
assert.deepEqual(swipe.steps,[1,-1]);assert.equal(swipe.failed,0);
swipe.accept=false;swipe.event({input:{dir:1}});assert.equal(swipe.failed,1);
module.exports = {target, dsp, history};
console.log('PASS: actual C selection/DSP/gesture bodies: repeat, shuffle, manual override, flat PCM, band response, saturation, stereo isolation, filter continuity and horizontal-only swipe.');

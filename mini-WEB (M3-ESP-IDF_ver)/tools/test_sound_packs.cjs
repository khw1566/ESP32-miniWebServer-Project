// Host checks for synthesized sound sets: spec, generated table, settings list and names. No build or codec.
const fs=require('fs'),path=require('path'),assert=require('assert');
const root=path.resolve(__dirname,'..'),read=p=>fs.readFileSync(path.join(root,p),'utf8');
const spec=JSON.parse(read('tools/sound_packs.json')),table=read('main/m3_sound_packs.inc');
const header=read('main/include/m3_feedback.h'),feedback=read('main/m3_feedback.c'),picker=read('main/m3_sound_ui.inc');
const count=Number(/#define M3_SOUND_PACK_COUNT (\d+)/.exec(header)[1]);
assert.equal(spec.packs.length,count,'M3_SOUND_PACK_COUNT matches the spec');
assert.equal(spec.packs[0].id,'classic','saved out-of-range choices fall back to the original set');
// The generated table must match the spec exactly (run tools/generate_sound_packs.py after edits).
for(const pack of spec.packs)for(const cue of ['boot','tap','confirm','error','setup']){
 const line=new RegExp(`static const m3_tone_t ${pack.id}_${cue}\\[\\] = \\{(.*)\\};`).exec(table);
 assert(line,`${pack.id}.${cue} missing from m3_sound_packs.inc`);
 const notes=[...line[1].matchAll(/\{(\d+), (\d+), M3_WAVE_(\w+), (\d+)\}/g)].map(m=>[+m[1],+m[2],m[3].toLowerCase(),+m[4]]);
 assert.deepEqual(notes,pack[cue],`${pack.id}.${cue} is stale; rerun tools/generate_sound_packs.py`);
}
const names=[.../T_PACK_(\w+)/g[Symbol.matchAll](/names\[M3_SOUND_PACK_COUNT\] = \{([^}]*)\}/.exec(picker)[1])].map(m=>m[1].toLowerCase());
assert.deepEqual(names,spec.packs.map(p=>p.id),'settings list order follows the spec');
for(const id of names)assert(read('main/include/m3_i18n.h').includes(`TEXT(PACK_${id.toUpperCase()})`));
// Firmware: the selection is bounded, persisted beside the eight-byte blob and read once per cue.
assert(/value >= M3_SOUND_PACK_COUNT\) return;/.test(feedback));
assert(/selected < M3_SOUND_PACK_COUNT\) pack = selected;/.test(feedback));
assert(feedback.includes('nvs_set_u8(nvs, "sound_pack"'));
assert(feedback.includes('&sound_packs[m3_feedback_pack()]'));
assert(read('main/m3_ui.c').includes('sound_pack_list(box);'));
assert(read('tools/ui_sim/sim_main.c').includes('unsigned m3_feedback_pack(void)'),'simulator links the picker');
const notes=spec.packs.reduce((n,p)=>n+['boot','tap','confirm','error','setup'].reduce((k,c)=>k+p[c].length,0),0);
console.log(`PASS: ${count} sound sets, ${notes} notes in sync with the firmware table, bounded saved choice and settings list.`);

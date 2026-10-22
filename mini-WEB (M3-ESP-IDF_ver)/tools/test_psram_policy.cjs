// Placement/source policies and map-parser fixtures; no native or firmware build.
const fs=require('fs'),path=require('path'),assert=require('assert'),{spawnSync}=require('child_process');
const root=path.resolve(__dirname,'..'),read=p=>fs.readFileSync(path.join(root,p),'utf8').replace(/\r\n/g,'\n');
const defaults=read('sdkconfig.defaults'),policy=read('cmake/memory_policy.cmake'),main=read('main/main.c');
for(const flag of ['CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=0','CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL=131072',
 'CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY=y','CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=y'])assert(defaults.includes(flag)&&policy.includes(flag),flag);
assert(main.indexOf('heap_caps_malloc_extmem_enable(0)')<main.indexOf('nvs_flash_init()'));
assert(policy.includes('CONFIG_MBEDTLS_DEFAULT_MEM_ALLOC CONFIG_MBEDTLS_INTERNAL_MEM_ALLOC CONFIG_MBEDTLS_CUSTOM_MEM_ALLOC'));
const engine=read('components/espeak_ng/linker.lf');assert(engine.includes('archive: libespeak_ng.a'));
assert(engine.includes('bss -> extern_ram'));assert(engine.includes('common -> extern_ram'));
assert(engine.includes('* (m3_espeak_psram)'));assert(!engine.includes('data -> extern_ram'),'initialized data is not blindly relocated');
assert(read('components/espeak_ng/CMakeLists.txt').includes('LDFRAGMENTS "linker.lf"'));
const ui=read('main/m3_ui.c'),agent=read('main/m3_agent.c'),tube=read('main/m3_tube.c'),radio=read('main/m3_radio.c');
const bindingSlots=/static M3_PSRAM_BSS binding_t bindings\[(\d+)\]/.exec(ui);
assert(bindingSlots && Number(bindingSlots[1]) >= 256, 'translation bindings retain capacity and PSRAM placement');
for(const item of ['m3_runtime_state_t snapshot','lv_buttonmatrix_ctrl_t key_controls[8][64]',
 'm3_tube_list_t tube_list','m3_agent_state_t agent','m3_voice_state_t voice'])assert(ui.includes('static M3_PSRAM_BSS '+item),item);
assert(agent.includes('static M3_PSRAM_BSS char history_user[2][385], history_answer[2][1537]'));
assert(tube.includes('static M3_PSRAM_BSS m3_tube_list_t list'));
assert(tube.includes('heap_caps_malloc(AUDIO_LIMIT, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)'));
assert(radio.includes('heap_caps_malloc(RADIO_PCM_BLOCK_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)'));
for(const source of [agent,tube,radio]){
 assert(!/static M3_PSRAM_BSS (?:portMUX_TYPE|atomic_)/.test(source));
 assert(!source.includes('xTaskCreatePinnedToCoreWithCaps'),'no unverified TLS/ROM stack migration');
}
const header=read('main/include/m3_memory.h');assert(header.includes('#ifdef ESP_PLATFORM'));
assert(header.includes('#if !CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY'));
assert(read('main/m3_board.c').includes('port.task_stack = 16384;'));
const scenario=read('tools/ui_sim/scenarios.inc');assert(scenario.includes('== APP_COUNT'));
assert(scenario.includes('"agent", "tube"'));
const build=read('tools/ui_sim/build.ps1');assert(build.includes('if ($compileExit -ne 0)'));
assert(build.indexOf('if ($compileExit -ne 0)')<build.indexOf('& "$Out\\m3_ui_sim.exe"'));
const python=String.raw`
import importlib.util
spec=importlib.util.spec_from_file_location('audit','tools/audit_psram.py')
audit=importlib.util.module_from_spec(spec);spec.loader.exec_module(audit)
internal=' .bss.speech\n                0x4ff40000       0x100 esp-idf/espeak_ng/libespeak_ng.a(speech.c.obj)\n'
external=' .bss.speech\n                0x48000000       0x100 esp-idf/espeak_ng/libespeak_ng.a(speech.c.obj)\n'
discard=' .bss.unused 0x00000000 0x100 esp-idf/espeak_ng/libespeak_ng.a(unused.c.obj)\n'
app=' .ext_ram.bss.3 0x48000100 0x80 esp-idf/main/libmain.a(m3_ui.c.obj)\n'
r=audit.inspect_map(internal+discard+app)
assert r['espeak_bss']['internal']==256 and not r['espeak_bss_in_psram']
assert r['libraries']['libmain.a']['psram']==128
r=audit.inspect_map(external+discard+app)
assert r['espeak_bss']['psram']==256 and r['espeak_bss_in_psram']
assert len(r['sections'])==2
assert not audit.inspect_map('unrelated or incomplete map')['espeak_bss_in_psram']
assert not audit.inspect_map(internal+external)['espeak_bss_in_psram']
print('PASS: map parser distinguishes internal/external/discarded BSS and rejects mixed or missing engine placement.')
`;
const result=spawnSync(process.env.PYTHON || 'python',['-c',python],{cwd:root,encoding:'utf8'});
assert.equal(result.status,0,result.error?.message || result.stdout+result.stderr);
process.stdout.write(result.stdout);
for(const file of ['AGENTS.md','docs/OPTIMIZATION_AND_VALIDATION.md']){
 const doc=read(file);assert(doc.includes('PSRAM'));assert(doc.includes('시뮬레이터'));assert(doc.includes('실기'));
}
console.log('PASS: generic/TLS/BSS policies, external engine/cache/history/PCM placement, internal control and stacks, simulator app coverage and stale-EXE rejection, M3 instructions.');

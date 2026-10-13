// CMake LANGUAGES NONE fixtures and source policy execution; no firmware compiler.
const fs=require('fs'),path=require('path'),vm=require('vm'),assert=require('assert');
const {spawnSync}=require('child_process');
const root=path.resolve(__dirname,'..'),read=p=>fs.readFileSync(path.join(root,p),'utf8').replace(/\r\n/g,'\n');
const slash=p=>p.replace(/\\/g,'/'),write=(p,s)=>{fs.mkdirSync(path.dirname(p),{recursive:true});fs.writeFileSync(p,s);};
const original=read('managed_components/espressif__esp_hosted/host/drivers/transport/sdio/sdio_drv.c');
const saved='CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=1024\nCONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL=32768\nCONFIG_UNRELATED_USER_CHOICE=17\n# CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY is not set\nCONFIG_MBEDTLS_DEFAULT_MEM_ALLOC=y\n# CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC is not set\nCONFIG_FATFS_API_ENCODING_ANSI_OEM=y\n# CONFIG_FATFS_API_ENCODING_UTF_8 is not set\n';
function fixture(name,{version='1.4.1',source=original,existing=true,success=true}={}){
 const dir=path.join(root,'results/m3-dma-memory',name),hosted=path.join(dir,'hosted');
 write(path.join(hosted,'idf_component.yml'),'version: '+version+'\n');
 write(path.join(hosted,'host/drivers/transport/sdio/sdio_drv.c'),source);
 write(path.join(dir,'unchanged.c'),'/* unrelated fixture source */');
 if(existing)write(path.join(dir,'sdkconfig'),saved);
 write(path.join(dir,'CMakeLists.txt'),`cmake_minimum_required(VERSION 3.16)
include("${slash(root)}/cmake/memory_policy.cmake")
m3_memory_policy()
project(dma_fixture LANGUAGES NONE)
function(idf_component_get_property out component property)
  if(property STREQUAL "COMPONENT_DIR")
    set(${'$'}{out} "${slash(hosted)}" PARENT_SCOPE)
  else()
    set(${'$'}{out} hosted_fixture PARENT_SCOPE)
  endif()
endfunction()
add_custom_target(hosted_fixture)
set_property(TARGET hosted_fixture PROPERTY SOURCES "${slash(hosted)}/host/drivers/transport/sdio/sdio_drv.c;unchanged.c")
include("${slash(root)}/cmake/hosted_dma_guard.cmake")
m3_hosted_dma_guard()
get_target_property(sources hosted_fixture SOURCES)
file(WRITE "${'$'}{CMAKE_BINARY_DIR}/sources.txt" "${'$'}{sources}")
`);
 const run=()=>spawnSync('cmake',['-S',dir,'-B',path.join(dir,'configured')],{encoding:'utf8'});
 let result=run();assert.equal(result.status===0,success,result.stdout+result.stderr);
 assert.equal(fs.readFileSync(path.join(hosted,'host/drivers/transport/sdio/sdio_drv.c'),'utf8'),source,'registry source unchanged');
 if(!success)return;
 const generated=path.join(dir,'configured/m3_hosted_dma_guard/sdio_drv.c');
 const selected=fs.readFileSync(path.join(dir,'configured/sources.txt'),'utf8');assert(selected.includes(slash(generated))&&selected.includes('unchanged.c'));
 if(existing){const config=fs.readFileSync(path.join(dir,'sdkconfig'),'utf8').replace(/\r\n/g,'\n');
  assert(config.includes('CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=0\n'));
  assert(config.includes('CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL=131072\n'));
  assert(config.includes('CONFIG_UNRELATED_USER_CHOICE=17\n'));
  assert(config.includes('CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY=y'));
  assert(config.includes('CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=y'));
  assert(config.includes('# CONFIG_MBEDTLS_DEFAULT_MEM_ALLOC is not set'));
  assert(config.includes('CONFIG_FATFS_API_ENCODING_UTF_8=y'));
  assert(config.includes('# CONFIG_FATFS_API_ENCODING_ANSI_OEM is not set'));
  assert(!config.includes('\nCONFIG_FATFS_API_ENCODING_ANSI_OEM=y'));
  assert(!config.includes('\nCONFIG_MBEDTLS_DEFAULT_MEM_ALLOC=y')); }
 else assert(!fs.existsSync(path.join(dir,'sdkconfig')),'new project keeps SDK defaults workflow');
 const stamp=fs.statSync(generated).mtimeMs;
 result=run();assert.equal(result.status,0,result.stdout+result.stderr);assert.equal(fs.statSync(generated).mtimeMs,stamp,'generated source is idempotent');
 return fs.readFileSync(generated,'utf8');
}
const generated=fixture('actual-component');fixture('new-config',{existing:false});
fixture('wrong-version',{version:'1.5.0',success:false});
fixture('changed-source',{source:original.replace('assert(pkt_rxbuff);','assert(other);'),success:false});
assert.equal(read('managed_components/espressif__esp_hosted/host/drivers/transport/sdio/sdio_drv.c'),original);
const start=generated.indexOf('/* M3: give'),end=generated.indexOf('packet_size = len + offset;',start);
assert(start>0&&end>start);const guard=generated.slice(start,end);
const context={NULL:null,MEMSET_REQUIRED:1,ESP_ERR_NO_MEM:0x101,TAG:'test',attempts:[],delays:[],logs:[],
 sdio_buffer_alloc:()=>context.attempts.shift()??null,g_h:{funcs:{_h_msleep:n=>context.delays.push(n)}},ESP_LOGE:(...args)=>context.logs.push(args)};
vm.createContext(context);vm.runInContext('function alloc(){let pkt_rxbuff;'+guard
 .replace('unsigned retry','let retry').replaceAll('g_h.funcs->','g_h.funcs.')+'return pkt_rxbuff;}',context);
const buffer={valid:true};context.attempts=[buffer];assert.equal(context.alloc(),buffer);assert.equal(context.delays.length,0);
context.attempts=[null,null,buffer];assert.equal(context.alloc(),buffer);assert.deepEqual(context.delays,[10,10]);
context.delays=[];context.logs=[];context.attempts=[];
assert.equal(context.alloc(),context.ESP_ERR_NO_MEM);assert.deepEqual(context.delays,[10,10,10]);assert.equal(context.logs.length,1);
assert(!guard.includes('assert('),'allocation failure cannot enter the observed assertion');
const main=read('main/main.c'),audio=read('main/m3_audio.c'),board=read('main/m3_board.c');
assert(main.indexOf('heap_caps_malloc_extmem_enable(0)')<main.indexOf('m3_board_init()'));
assert(main.indexOf('heap_caps_malloc_extmem_enable(0)')<main.indexOf('nvs_flash_init()'));
assert(main.includes('largest DMA=%u'));assert(board.includes('port.task_stack = 16384;'),'keep UI stack overflow fix');
const descriptors=Number(/AUDIO_DMA_DESCS = (\d+)/.exec(audio)[1]),frames=Number(/AUDIO_DMA_FRAMES = (\d+)/.exec(audio)[1]);
assert.equal(descriptors*frames*4*2,11520,'TX+RX sample storage uses 11.25 KiB before alignment/descriptors');
assert.equal(8*480*4*2-descriptors*frames*4*2,19200,'previous DMA growth costs 18.75 KiB of shared internal RAM');
const defaults=read('sdkconfig.defaults');assert(defaults.includes('CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=0'));
assert(defaults.includes('CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL=131072'));
const cmake=read('CMakeLists.txt');assert(cmake.indexOf('m3_memory_policy()')<cmake.indexOf('project(mini-webserver-m3)'));
assert(cmake.includes('m3_hosted_dma_guard()'));assert(read('m3.ps1').includes("Join-Path $PSScriptRoot 'cmake'"));
console.log('PASS: saved SDK memory migration/idempotence, unchanged user settings, generated Hosted guard/version/source checks, transient/permanent DMA allocation failures, early PSRAM preference, preserved UI stack, 18.75 KiB DMA sample-storage reduction.');

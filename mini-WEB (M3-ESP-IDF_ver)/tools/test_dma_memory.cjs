// CMake LANGUAGES NONE fixtures and source policy execution; no firmware compiler.
const fs=require('fs'),path=require('path'),vm=require('vm'),assert=require('assert');
const {spawnSync}=require('child_process');
const root=path.resolve(__dirname,'..'),read=p=>fs.readFileSync(path.join(root,p),'utf8').replace(/\r\n/g,'\n');
const slash=p=>p.replace(/\\/g,'/'),write=(p,s)=>{fs.mkdirSync(path.dirname(p),{recursive:true});fs.writeFileSync(p,s);};
const saved='CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=1024\nCONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL=32768\nCONFIG_UNRELATED_USER_CHOICE=17\n# CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY is not set\nCONFIG_MBEDTLS_DEFAULT_MEM_ALLOC=y\n# CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC is not set\nCONFIG_FATFS_API_ENCODING_ANSI_OEM=y\n# CONFIG_FATFS_API_ENCODING_UTF_8 is not set\n';
function fixture(name,{existing=true}={}){
 const dir=path.join(root,'results/m3-dma-memory',name);
 if(existing)write(path.join(dir,'sdkconfig'),saved);
 write(path.join(dir,'CMakeLists.txt'),`cmake_minimum_required(VERSION 3.16)
include("${slash(root)}/cmake/memory_policy.cmake")
m3_memory_policy()
project(dma_fixture LANGUAGES NONE)
`);
 const run=()=>spawnSync('cmake',['-S',dir,'-B',path.join(dir,'configured')],{encoding:'utf8'});
 let result=run();assert.equal(result.status,0,result.stdout+result.stderr);
 if(existing){const config=fs.readFileSync(path.join(dir,'sdkconfig'),'utf8').replace(/\r\n/g,'\n');
  assert(config.includes('CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=0\n'));
  assert(config.includes('CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL=131072\n'));
  assert(config.includes('CONFIG_UNRELATED_USER_CHOICE=17\n'));
  assert(config.includes('CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY=y'));
  assert(config.includes('CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=y'));
  assert(config.includes('# CONFIG_MBEDTLS_DEFAULT_MEM_ALLOC is not set'));
  assert(config.includes('CONFIG_FATFS_API_ENCODING_UTF_8=y'));
  assert(config.includes('# CONFIG_FATFS_API_ENCODING_ANSI_OEM is not set'));
  assert(config.includes('# CONFIG_ESP_H264_DECODER_IRAM is not set'));
  assert(config.includes('# CONFIG_ESP_H264_DUAL_TASK is not set'));
  assert(!config.includes('\nCONFIG_FATFS_API_ENCODING_ANSI_OEM=y'));
  assert(!config.includes('\nCONFIG_MBEDTLS_DEFAULT_MEM_ALLOC=y')); }
 else assert(!fs.existsSync(path.join(dir,'sdkconfig')),'new project keeps SDK defaults workflow');
 if(existing){const once=fs.readFileSync(path.join(dir,'sdkconfig'),'utf8');
  result=run();assert.equal(result.status,0,result.stdout+result.stderr);
  assert.equal(fs.readFileSync(path.join(dir,'sdkconfig'),'utf8'),once,'saved config migration is idempotent');}
}
fixture('saved-config');fixture('new-config',{existing:false});
/* Hosted 2.13 skips an RX packet when its DMA mempool is empty instead of
 * asserting, which replaces the 1.4.1-only M3 guard. */
const hostedDir=path.join(root,'managed_components/espressif__esp_hosted');
if(fs.existsSync(path.join(hostedDir,'idf_component.yml'))&&/version:\s*["']?2\.13\.0/.test(read('managed_components/espressif__esp_hosted/idf_component.yml'))){
 const sdio=read('managed_components/espressif__esp_hosted/host/drivers/transport/sdio/sdio_drv.c');
 const alloc=sdio.indexOf('pkt_rxbuff = sdio_buffer_alloc(MEMSET_REQUIRED);');
 assert(alloc>0&&sdio.slice(alloc,alloc+200).includes('if (!pkt_rxbuff)'),'Hosted RX allocation failure is handled');
 assert(!sdio.slice(alloc,alloc+200).includes('assert(pkt_rxbuff)'));
}
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
assert(!cmake.includes('hosted_dma_guard'),'1.4.1-only guard is retired with Hosted 2.13');assert(read('m3.ps1').includes("Join-Path $PSScriptRoot 'cmake'"));
console.log('PASS: saved SDK memory migration/idempotence, unchanged user settings, Hosted 2.13 RX allocation failure handling, early PSRAM preference, preserved UI stack, 18.75 KiB DMA sample-storage reduction.');

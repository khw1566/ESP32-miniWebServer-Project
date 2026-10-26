// Execute the actual wrapper/start policy with a mocked SDK init and heap.
// No firmware compilation, SDK writes, C6 connection or hardware access.
const fs=require('fs'),path=require('path'),assert=require('assert'),vm=require('vm');
const root=path.resolve(__dirname,'..');
const read=name=>fs.readFileSync(path.join(root,name),'utf8');
const source=read('main/m4_hosted_boot.c');
function body(name){
 const m=new RegExp('\\b'+name+'\\([^)]*\\)\\s*\\{').exec(source);assert(m,name);
 let depth=1,start=m.index+m[0].length,end=start;
 while(depth){if(source[end]==='{')depth++;if(source[end]==='}')depth--;end++;}
 return source.slice(start,end-1).replace(/\/\*[\s\S]*?\*\//g,'')
  .replace('atomic_load(&app_ready)','app_ready').replace('atomic_store(&app_ready, true)','app_ready = true')
  .replaceAll('(unsigned)','');
}
const ctx={app_ready:false,ESP_OK:0,calls:0,result:0,logs:0,
 MALLOC_CAP_INTERNAL:1,MALLOC_CAP_8BIT:2,MALLOC_CAP_DMA:4,
 heap_caps_get_free_size:()=>131072,heap_caps_get_largest_free_block:()=>65536,
 ESP_LOGI:()=>ctx.logs++,__real_esp_hosted_init:()=>{ctx.calls++;return ctx.result;}};
vm.createContext(ctx);
vm.runInContext('function __wrap_esp_hosted_init(){'+body('__wrap_esp_hosted_init')+'}\n'+
 'function m4_hosted_start(){'+body('m4_hosted_start')+'}',ctx);
assert.equal(ctx.__wrap_esp_hosted_init(),0);assert.equal(ctx.calls,0,'constructor must not allocate a DMA pool');
assert.equal(ctx.__wrap_esp_hosted_init(),0);assert.equal(ctx.calls,0,'all pre-app init requests stay deferred');
assert.equal(ctx.m4_hosted_start(),0);assert.equal(ctx.calls,1);assert.equal(ctx.logs,1);
assert.equal(ctx.__wrap_esp_hosted_init(),0);assert.equal(ctx.calls,2,'subsequent SDK calls reach its idempotent implementation');
ctx.result=0x101;assert.equal(ctx.__wrap_esp_hosted_init(),0x101,'SDK error must propagate');
const main=read('main/main.c'),cmake=read('main/CMakeLists.txt');
assert(main.indexOf('m4_hosted_start();')>main.indexOf('nvs_flash_init();'));
assert(main.indexOf('m4_hosted_start();')<main.indexOf('m4_board_init();'));
assert(main.indexOf('m4_hosted_start();')<main.indexOf('m4_preferences_web_active()'));
assert(cmake.includes('"m4_hosted_boot.c"'));
assert(cmake.includes('"-Wl,--wrap=esp_hosted_init"'));
assert(cmake.includes('"-Wl,--undefined=__wrap_esp_hosted_init"'));
console.log('PASS: defer constructor allocations, start after main_task heap reclamation, SDK error propagation, headless ordering and wrapper extraction root.');

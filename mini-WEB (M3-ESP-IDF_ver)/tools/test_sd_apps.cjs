// Execute actual small C policies with host mocks; no target compilation.
const fs=require('fs'),path=require('path'),vm=require('vm'),assert=require('node:assert/strict');
const root=path.resolve(__dirname,'..'),read=p=>fs.readFileSync(path.join(root,p),'utf8');
const src=read('main/m3_sd_apps.c'),ui=read('main/m3_ui.c');
function body(name){const m=new RegExp('\\b'+name+'\\([^)]*\\)\\s*\\{').exec(src);assert(m,name);
 let i=m.index+m[0].length,start=i,n=1;while(n){if(src[i]==='{')n++;if(src[i]==='}')n--;i++;}return src.slice(start,i-1);}
const c={M3_SD_APP_NAME:48,strlen:s=>s.length,strcmp:(a,b)=>a===b?0:1,HEAP_LIMIT:768*1024,
 NULL:null,MALLOC_CAP_SPIRAM:1,MALLOC_CAP_8BIT:2,freed:[],now:0,cancel:false,
 free:p=>c.freed.push(p),heap_caps_realloc:(p,n)=>c.fail?null:{size:n},
 context:L=>L,atomic_load:()=>c.cancel,esp_timer_get_time:()=>c.now,luaL_error:()=>{throw Error('budget');}};
vm.createContext(c);
let valid=body('valid_name').replace(/size_t /g,'let ').replace(/name \+ n - 4/g,'name.slice(n - 4)');
vm.runInContext('function valid(name){'+valid+'}',c);
for(const s of ['sand_tetris.lua','A-1.lua','a.lua','x'.repeat(43)+'.lua'])assert(c.valid(s),s);
for(const s of ['../a.lua','a/b.lua','a\\b.lua','.lua','a.LUA','a.lua.exe','x'.repeat(44)+'.lua','a.b.lua','a b.lua'])assert(!c.valid(s),s);
c.valid_name=c.valid;c.SOURCE_LIMIT=65536;c.S_ISREG=mode=>mode==='file';
c.memchr=(source,value,n)=>source.slice(0,n).includes(value)?1:0;
vm.runInContext('function eligible(name,info){'+body('file_eligible').replaceAll('->','.')+'} function sourceValid(source,n,io_error){'+body('source_valid')+'}',c);
for(const n of [1,65536]) assert(c.eligible('sand.lua',{st_mode:'file',st_size:n}));
for(const n of [-1,0,65537]) assert(!c.eligible('sand.lua',{st_mode:'file',st_size:n}));
assert(!c.eligible('sand.lua',{st_mode:'dir',st_size:1}));
assert(!c.eligible('../sand.lua',{st_mode:'file',st_size:1}));
assert(c.sourceValid(Buffer.from('return 1'),8,false));
assert(!c.sourceValid(Buffer.from([97,0,98]),3,false));
assert(!c.sourceValid(Buffer.alloc(0),0,false));
assert(!c.sourceValid(Buffer.alloc(65537,32),65537,false));
assert(!c.sourceValid(Buffer.from('return 1'),8,true));
let alloc=body('allocate').replace(/\/\*[\s\S]*?\*\//g,'').replace('vm_t *vm = ud','let vm = ud')
 .replace('void *next =','let next =').replaceAll('->','.');
vm.runInContext('function allocate(ud,ptr,old,size){'+alloc+'}',c);
const mem={used:0};let p=c.allocate(mem,null,6,100);assert.equal(mem.used,100,'new allocation ignores Lua type tag');
let q=c.allocate(mem,p,100,300);assert.equal(mem.used,300);
assert.equal(c.allocate(mem,q,300,c.HEAP_LIMIT+1),null);assert.equal(mem.used,300);
c.fail=true;assert.equal(c.allocate(mem,q,300,400),null);assert.equal(mem.used,300);c.fail=false;
assert.equal(c.allocate(mem,q,300,0),null);assert.equal(mem.used,0);
let hook=body('budget').replace('(void)debug;','').replace('vm_t *vm = context(L)','let vm = context(L)').replaceAll('->','.').replace('&cancel','cancel');
vm.runInContext('function budget(L,debug){'+hook+'}',c);
let state={instructions:0,deadline:100};c.budget(state);assert.equal(state.instructions,1000);
state.instructions=600000;assert.throws(()=>c.budget(state),/budget/);
state.instructions=0;c.now=101;assert.throws(()=>c.budget(state),/budget/);
c.now=0;c.cancel=true;assert.throws(()=>c.budget(state),/budget/);
// Lifecycle/source integration contracts supplement the executable policies.
assert(src.includes('luaL_loadbufferx(L, source, strlen(source), selected, "t")'));
for(const name of ['dofile','loadfile','load','pcall','xpcall','setmetatable','getmetatable'])assert(body('configure').includes('"'+name+'"'));
for(const lib of ['luaopen_io','luaopen_os','luaopen_debug','luaopen_package','luaopen_coroutine'])assert(!src.includes(lib));
assert(body('read_source').includes('sd_media_admin_lock()'));assert(body('read_source').includes('sd_media_admin_unlock()'));
assert(src.includes('source_valid(source, n, ferror(file) != 0)'));assert(src.includes('next_scan = now + 3000000'));
assert(body('worker').indexOf('front = NULL')<body('worker').indexOf('free(frame)'));
assert(body('worker').includes('lua_close(L)'));assert(body('worker').includes('atomic_store(&busy, false)'));
assert(ui.includes('sd_apps_release();'));assert(ui.includes('lv_canvas_set_buffer(sd_canvas, sd_pixels'));
assert(read('main/CMakeLists.txt').includes('"m3_sd_apps.c"'));assert(read('main/CMakeLists.txt').includes('PRIV_REQUIRES m3_lua'));
assert(read('main/m3_runtime.c').includes('m3_photos_poll(); m3_sd_apps_poll();'));
console.log('SD apps: actual C filename, bounded allocator and cancellation/instruction/deadline policies passed; source/lifecycle integration checked.');

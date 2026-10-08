const fs=require('fs'),vm=require('vm'),assert=require('assert'),path=require('path');
const boards=['mini-WEB (M1-ESP-IDF_ver)','mini-WEB (S3-N16R8-SD-ESP-IDF_ver)','mini-WEB (S3-ESP-IDF_ver)','mini-WEB (C3-ESP-IDF_ver)'];
function strings(source){return [...source.matchAll(/"(?:\\.|[^"\\])*"/g)].map(m=>JSON.parse(m[0])).join('');}
function page(board){let src=fs.readFileSync(path.join(board,'main/web_page_admin.c'),'utf8').split('const char HTML_ADMIN_DASHBOARD[] =')[1];if(src.includes('SD_ADMIN_SCRIPT')){const defs=fs.readFileSync(path.join(board,'main/web_sd_admin_ui.inc'),'utf8');for(const name of ['SD_ADMIN_HTML','SD_ADMIN_SCRIPT'])src=src.replace(name,defs.split('#define '+name)[1].split('#define')[0].replace(/\\\r?\n/g,''));}return strings(src);}
class El{
 constructor(){this.children=[];this.style={};this.attrs={};this.events={};this.value='';this.classList={add(){},remove(){}};}
 set textContent(t){this.text=t;this.children=[];}get textContent(){return this.text||'';}
 insertRow(){const el=new El();this.children.push(el);return el;}insertCell(){return this.insertRow();}
 appendChild(e){this.children.push(e);}setAttribute(k,v){this.attrs[k]=v;}
 addEventListener(k,f){(this.events[k]||(this.events[k]=[])).push(f);}remove(){}click(){}
}
function setup(){const els={},calls=[],saves=[],alerts=[];const el=id=>els[id]||(els[id]=new El());
 const ctx={document:{getElementById:el,createElement:()=>new El(),body:new El()},fmtB:n=>n+' B',uiErrorText:e=>e.message,g_adminPw:'test',g_adminSession:false,g_uploading:false,g_collecting:false,uploadControls(){},showGlo(){},hideGlo(){},updateStats(){},alert:s=>alerts.push(s),confirm:()=>true,setTimeout(){},URL:{createObjectURL:()=>'',revokeObjectURL(){}},fetch:async(url,options)=>{calls.push({url,options});return {ok:true,blob:async()=>({})};}};
 vm.createContext(ctx);return {ctx,el,calls,saves,alerts};}
function shared(html,ctx){const script=[...html.matchAll(/<script>([\s\S]*?)<\/script>/g)].map(m=>m[1]).find(s=>s.includes('var g_fileItems='));vm.runInContext(script.slice(script.indexOf('var g_fileItems='),script.indexOf('function fetchFiles('))+script.slice(script.indexOf('\nvar fileSelection='),script.indexOf('function uploadControls(')),ctx);}
(async()=>{
for(const board of boards){const html=page(board);for(const script of html.matchAll(/<script>([\s\S]*?)<\/script>/g))new vm.Script(script[1]);
 const {ctx:c,el,calls,saves}=setup();shared(html,c);c.saveFileResponse=async(r,name)=>{if(!r.ok)throw Error('failed');saves.push(name);};
 c.g_fileItems=[{name:'index.html',size:20},{name:'사진.png',size:30},{name:'notes.txt',size:40}];el('fileSort').value='name';el('fileDirection').value='asc';c.renderCachedFiles();assert.equal(el('fileList').children.length,3);assert.equal(el('fileList').children[0].children.length,8);
 c.selectVisibleFiles(true);assert.equal(c.fileSelection.size,3);el('fileSearch').value='notes';c.renderCachedFiles();c.selectVisibleFiles(false);assert.equal(c.fileSelection.size,2,'filtered selection only changes visible files');assert.equal(el('fileSelectAll').checked,false);
 await c.fileBulk('download',['notes.txt']);assert.deepEqual(saves,['notes.txt'],'single save does not download the other selections');
 let active=0,maxActive=0;c.fetch=async(url,options)=>{calls.push({url,options});active++;maxActive=Math.max(active,maxActive);await new Promise(r=>setImmediate(r));active--;return {ok:!url.includes('index.html'),text:async()=>'write failed'};};
 c.fetchFiles=async()=>{c.g_fileItems=c.g_fileItems.filter(f=>!f.name.includes('사진'));};await c.fileBulk('delete');assert.equal(maxActive,1);assert.equal(c.fileSelection.size,1);assert(c.fileSelection.has('index.html'));assert.equal(c.fileBulkBusy,false);
 const before=calls.length;c.g_uploading=true;await c.fileBulk('delete');assert.equal(calls.length,before);c.g_uploading=false;
 el('fileDateFrom').value='2026-10-08';el('fileDateTo').value='2026-10-07';c.renderCachedFiles();assert.equal(c.fileVisible.length,0);assert(el('fileSelectAll').disabled);
 const server=fs.readFileSync(path.join(board,'main/web_server.c'),'utf8');assert(server.includes('.uri="/api/files/download"'));assert(server.includes('char buffer[1024];size_t n;'));assert(server.includes('Content-Disposition","attachment"'));
 console.log('PASS '+board+': script syntax, filtered selection, single save, sequential partial deletion, busy guards.');
 if(!html.includes('sdSelectAll'))continue;
 const sd=fs.readFileSync(path.join(board,'main/web_sd_admin_ui.inc'),'utf8');vm.runInContext(strings(sd.split('#define SD_ADMIN_SCRIPT')[1]),c);
 c.g_adminPw='';c.g_adminSession=true;c.fetch=async(url,options)=>{calls.push({url,options});return {ok:true,json:async()=>({page_size:24,count:50,total:1000,used:250,cached:true,files:[{name:'media',directory:true},{name:'a.txt',size:1},{name:'사진.png',size:2}]}),blob:async()=>({})};};
 await c.sdRefresh();assert.equal(c.sdPageSize,24);assert.equal(el('sdFiles').children.length,3);assert.equal(el('sdNext').disabled,false);assert(el('sdMessage').textContent.includes('미리 조회'));
 c.sdSelectVisible(true);await new Promise(r=>setImmediate(r));assert.equal(c.sdSelection.size,2);assert(!c.sdSelection.has('media'));
 c.sdFolder='/media';await c.sdRefresh();assert.equal(c.sdSelection.size,0,'folder changes clear selection');
 c.sdSelection.add('a.txt');c.sdSelection.add('사진.png');const originalFetch=c.fetch;c.fetch=async(url,options)=>options&&options.method==='DELETE'?{ok:!url.includes('a.txt'),text:async()=>'busy'}:originalFetch(url,options);
 await c.sdBulk('delete');assert(c.sdSelection.has('a.txt'));assert(!c.sdSelection.has('사진.png'));assert.equal(c.sdBusy,false);assert(el('sdMessage').textContent.includes('실패 1개'));
 const index=fs.readFileSync(path.join(board,'main/web_sd_index.inc'),'utf8'),api=fs.readFileSync(path.join(board,'main/web_sd_admin.inc'),'utf8'),media=fs.readFileSync(path.join(board,'main/sd_media.c'),'utf8');
 assert(index.includes('#define SD_WEB_PAGE_SIZE 24'));assert(index.includes('atomic_load(&sd_web_connections)==0'));assert(index.includes('SD_INDEX_RESERVE+bytes'));assert(index.includes('sd_index_complete && sd_index_revision==sd_media_revision()'));assert(index.includes('capacity>SD_INDEX_MAX'));assert(index.includes('free_bytes<48U*1024U'));assert(api.match(/sd_media_changed\(\)/g).length===2);assert(media.includes('sd_media_changed();session[0]=0;status=200;message="Upload complete";'));assert(api.includes('st.st_size>1024*1024'));
 // Run the idle policy from the C source without compiling firmware.
 const idleBody=index.match(/static bool sd_index_idle\(void\) \{([\s\S]*?)\n\}/)[1]
  .replaceAll('atomic_load(&sd_web_connections)','connections')
  .replaceAll('atomic_load(&sd_web_last_close)','lastClose')
  .replace('(TickType_t)(xTaskGetTickCount()-lastClose)','((xTaskGetTickCount()-lastClose)>>>0)');
 const state={running:true,tick:10000,connections:0,lastClose:0};
 function idle(){return vm.runInNewContext('(function(){'+idleBody+'})()', {web_server_is_running:()=>state.running,xTaskGetTickCount:()=>state.tick,pdMS_TO_TICKS:x=>x,connections:state.connections,lastClose:state.lastClose});}
 assert(idle());state.connections=1;assert(!idle());state.connections=0;state.tick=9999;assert(!idle());state.tick=10000;state.running=false;assert(!idle());state.running=true;state.lastClose=0xfffffff0;state.tick=(state.lastClose+10000)>>>0;assert(idle(),'idle delay survives tick wrap');
 console.log('PASS '+board+': SD 24-row pages, cookie authentication, directory selection, partial failure, idle policy, memory guards and upload invalidation.');
}
})().catch(e=>{console.error(e);process.exitCode=1;});

// Browser logic and actual C commit/recovery bodies with filesystem mocks.
// Does not compile firmware or claim physical FAT power-failure guarantees.
const fs=require('fs'),path=require('path'),vm=require('vm'),assert=require('assert');
const strings=s=>[...s.matchAll(/"(?:\\.|[^"\\])*"/g)].map(m=>JSON.parse(m[0])).join('');
function body(source,name){
 const match=new RegExp('\\b'+name+'\\([^;]*?\\)\\s*\\{').exec(source);assert(match,name);
 let i=match.index+match[0].length,start=i,depth=1;
 while(depth){if(source[i]==='{')depth++;if(source[i]==='}')depth--;i++;}
 return source.slice(start,i-1);
}
class El{
 constructor(){this.children=[];this.style={};this.events={};this.attrs={};this.value='';this.hidden=false;this.disabled=false;this.classList={add(){},remove(){}};}
 set textContent(v){this.text=v;this.children=[];}get textContent(){return this.text||'';}
 setAttribute(k,v){this.attrs[k]=v;}appendChild(e){this.children.push(e);}insertRow(){const e=new El();this.appendChild(e);return e;}insertCell(){return this.insertRow();}
 addEventListener(k,f){this.events[k]=f;}focus(){}click(){}remove(){}
}
async function ui(board){
 const main=path.join(board,'main'),defs=fs.readFileSync(path.join(main,'web_sd_admin_ui.inc'),'utf8');
 const script=strings(defs.split('#define SD_ADMIN_SCRIPT')[1]);
 let page=fs.readFileSync(path.join(main,'web_page_admin.c'),'utf8').split('const char HTML_ADMIN_DASHBOARD[] =')[1];
 for(const macro of ['SD_ADMIN_HTML','SD_ADMIN_SCRIPT'])page=page.replace(macro,defs.split('#define '+macro)[1].split('#define')[0].replace(/\\\r?\n/g,''));
 const html=strings(page);for(const match of html.matchAll(/<script>([\s\S]*?)<\/script>/g))new vm.Script(match[1]);
 assert(html.includes("id='flashFilesCard' hidden"));assert(html.includes('function fetchFiles(){return Promise.resolve(true);}'));
 const els={},calls=[];const el=id=>els[id]||(els[id]=new El());
 const state={answer:'new.txt',confirm:true,fail:false,raw:Buffer.from('초기 내용\n'),etag:'"old"'};
 const ctx={document:{getElementById:el,createElement:()=>new El(),body:new El()},g_adminPw:'',g_adminSession:true,g_uploading:false,g_collecting:false,fileBulkBusy:false,
  Blob,TextDecoder,fmtB:n=>n+' B',uiErrorText:e=>e.message,uploadControls(){},prompt:()=>state.answer,confirm:()=>state.confirm,setTimeout(){},
  URL:{createObjectURL:()=>'',revokeObjectURL(){}},fetch:async(url,opts)=>{
   calls.push({url,opts});if(state.fail&&opts.method==='PUT')return {ok:false,status:412,text:async()=>'File changed; reopen it before saving'};
   return {ok:true,headers:{get:()=>state.etag},arrayBuffer:async()=>Uint8Array.from(state.raw).buffer,
    json:async()=>({count:1,page_size:24,total:1024,used:10,files:[{name:'한글.txt',size:10}]}),blob:async()=>new Blob(['fixture'])};
  }};
 vm.createContext(ctx);vm.runInContext(script,ctx);
 assert(ctx.sdTextName('한글.txt'));assert(ctx.sdTextName('index.html'));assert(!ctx.sdTextName('photo.jpg'));
 await ctx.sdEdit('한글.txt');assert.equal(el('sdEditorText').value,'초기 내용\n');assert.equal(ctx.sdEditorTarget,'/한글.txt');
 el('sdEditorText').value='수정한 내용';state.fail=true;await ctx.sdSaveEditor();
 assert.equal(el('sdEditorText').value,'수정한 내용');assert.equal(el('sdEditor').hidden,false);assert.equal(ctx.sdEditorSaving,false);
 let put=calls.find(c=>c.opts.method==='PUT');assert.equal(put.opts.headers['If-Match'],'"old"');assert.equal(put.opts.credentials,'same-origin');assert.equal(await put.opts.body.text(),'수정한 내용');
 state.confirm=false;ctx.sdCloseEditor();assert(!el('sdEditor').hidden,'dirty close asks before discarding');
 state.fail=false;await ctx.sdSaveEditor();assert(el('sdEditor').hidden);assert(!ctx.sdBusy);
 state.answer='empty.txt';await ctx.sdEdit();assert.equal(el('sdEditorText').value,'');await ctx.sdSaveEditor();
 const create=calls.find(c=>c.opts.method==='POST');assert(create);assert.equal(create.opts.body.size,0);assert(!create.opts.headers['If-Match']);
 state.answer='../private.txt';await ctx.sdEdit();assert.equal(ctx.sdEditorTarget,null);
 state.answer='new.txt';await ctx.sdEdit();el('sdEditorText').value='a'.repeat(256*1024+1);const count=calls.length;await ctx.sdSaveEditor();assert.equal(calls.length,count);ctx.sdCloseEditor(true);
 state.raw=Buffer.from([0xff]);await ctx.sdEdit('bad.txt');assert(el('sdEditorSave').disabled);ctx.sdCloseEditor(true);
 state.raw=Buffer.from('a\0b');await ctx.sdEdit('binary.txt');assert(el('sdEditorSave').disabled);ctx.sdCloseEditor(true);
 // Closing an editor while a read is pending must invalidate that old response.
 const fetch=ctx.fetch;let finish;
 ctx.fetch=async()=>({ok:true,headers:{get:()=>state.etag},arrayBuffer:()=>new Promise(resolve=>{finish=resolve;})});
 const loading=ctx.sdEdit('slow.txt');await new Promise(resolve=>setImmediate(resolve));ctx.sdCloseEditor(true);
 state.answer='fresh.txt';await ctx.sdEdit();el('sdEditorText').value='new draft';
 finish(new TextEncoder().encode('old delayed response').buffer);await loading;assert.equal(el('sdEditorText').value,'new draft');ctx.sdCloseEditor(true);ctx.fetch=fetch;
 const uploads=[];
 ctx.XMLHttpRequest=class{
  constructor(){this.upload={};this.headers={};}
  open(method,url){this.method=method;this.url=url;}setRequestHeader(k,v){this.headers[k]=v;}
  send(file){uploads.push(this);this.status=this.method==='POST'?409:200;this.responseText='Filename already exists';this.onload();}
 };
 state.confirm=true;await ctx.sdUpload([{name:'replace.bin',size:123}]);
 assert.deepEqual(uploads.map(x=>x.method),['POST','PUT']);assert.equal(uploads[1].headers['If-Match'],'"old"');assert(!ctx.sdBusy&&!ctx.g_uploading);
 ctx.g_adminSession=false;const before=calls.length;await ctx.sdEdit('denied.txt');assert.equal(calls.length,before);
 console.log('PASS '+board+': full script syntax, SD-only panel, UTF-8 editor, empty creation, If-Match, conflict/dirty-content preservation, stale reads, binary replacement, limits and authentication.');
}
function transaction(board){
 const main=path.join(board,'main'),source=fs.readFileSync(path.join(main,'web_sd_admin.inc'),'utf8');
 const upload=body(source,'sd_admin_upload');
 assert(upload.includes('req->method == HTTP_PUT'));assert(upload.includes('"If-Match"'));assert(upload.includes('412 Precondition Failed'));
 assert(upload.indexOf('strcmp(expected,version)')<upload.indexOf('fopen(temp,"wb")'));
 const commit=upload.slice(upload.indexOf('    if(ok && replacing'),upload.indexOf('    sd_media_admin_unlock();\n    if(!ok)'));
 const c={ok:true,replacing:true,full:'file',temp:'temp',backup:'backup',TAG:'test',changed:0,files:{},fail:new Set(),errno:0,ENOENT:2,
  rename:(from,to)=>{if(c.fail.has(from+'>'+to)||!(from in c.files)||to in c.files)return -1;c.files[to]=c.files[from];delete c.files[from];return 0;},
  unlink:p=>{delete c.files[p];return 0;},ESP_LOGE(){},sd_media_changed:()=>c.changed++,
  sd_admin_backup_path:(_full,b)=>{b.value='backup';return true;},
  stat:(p,st)=>{if(!(p in c.files)){c.errno=2;return -1;}st.st_mode=c.files[p]==='directory'?'dir':'file';return 0;},S_ISREG:mode=>mode==='file'};
 vm.createContext(c);vm.runInContext('function commit(){'+commit+'}',c);
 let recover=body(source,'sd_admin_recover').replace('char backup[288]; struct stat st;','let backup = {}, st = {};')
  .replace('sizeof(backup)','288').replaceAll('&st','st').replaceAll('stat(backup,','stat(backup.value,')
  .replaceAll('rename(backup,','rename(backup.value,').replaceAll('unlink(backup)','unlink(backup.value)');
 vm.runInContext('function recover(full){'+recover+'}',c);
 const run=(fail=[],ok=true,replacing=true)=>{Object.assign(c,{ok,replacing,changed:0,fail:new Set(fail),files:replacing?{file:'old',temp:'new'}:{temp:'new'}});c.commit();};
 run();assert(c.ok);assert.equal(c.files.file,'new');assert(!('backup' in c.files));assert.equal(c.changed,1);
 run([],false);assert.equal(c.files.file,'old');assert(!('temp' in c.files));
 run(['file>backup']);assert.equal(c.files.file,'old');assert(!c.ok);
 run(['temp>file']);assert.equal(c.files.file,'old');assert(!c.ok);
 run(['temp>file','backup>file']);assert.equal(c.files.backup,'old');assert(!('file' in c.files));c.fail.clear();assert(c.recover('file'));assert.equal(c.files.file,'old');
 c.files={file:'new',backup:'old'};assert(c.recover('file'));assert.equal(c.files.file,'new');assert(!('backup' in c.files));
 c.files={file:'directory',backup:'old'};assert(!c.recover('file'));assert.equal(c.files.backup,'old');
 run([],true,false);assert.equal(c.files.file,'new');assert(c.ok);
 const server=fs.readFileSync(path.join(main,'web_server.c'),'utf8');assert(!server.includes('esp_spiffs_'));assert(server.includes('#define STORAGE_BASE_PATH SD_MEDIA_ROOT'));
 assert(server.includes('.method=HTTP_PUT'));assert(server.includes('strstr(clean_name,"/.")'));assert(server.includes('strstr(page,"/.")'));
 for(const name of ['api_files_get_handler','api_upload_post_handler','api_delete_post_handler','api_file_download_handler'])assert(body(server,name).includes('410 Gone'));
 for(const name of ['get_initial_view_mode','set_initial_view_mode','get_initial_page','set_initial_page','is_valid_initial_page'])assert(body(server,name));
 assert(!body(server,'api_settings_post_handler').includes('esp_spiffs_format'));
 console.log('PASS '+board+': actual C transaction/recovery mocks, write/rename/rollback failures, backup retention, SD-only routes and private-path guards.');
}
(async()=>{for(const board of ['mini-WEB (M3-ESP-IDF_ver)','mini-WEB (P4-ESP-IDF_ver)']){await ui(board);transaction(board);}})().catch(e=>{console.error(e);process.exitCode=1;});

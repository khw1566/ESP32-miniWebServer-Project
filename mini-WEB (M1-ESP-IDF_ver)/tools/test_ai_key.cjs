const fs=require('fs'),path=require('path'),vm=require('vm'),assert=require('assert');
const main=path.resolve(__dirname,'../main');
const generated=fs.readFileSync(path.join(main,'web_page_ai.c'),'utf8');
const html=[...generated.slice(generated.indexOf('const char HTML_AI_STUDIO')).matchAll(/"(?:\\.|[^"\\])*"/g)].map(m=>JSON.parse(m[0])).join('');
for(const match of html.matchAll(/<script>([\s\S]*?)<\/script>/g))new vm.Script(match[1]);
const source=html.slice(html.indexOf('async function aiFetch('),html.indexOf('async function logoutAi('));
function setup(fetch){
 const elements={},el=id=>elements[id]||(elements[id]={value:'',disabled:false,textContent:''});
 const context={fetch,location:{replace(){}},confirm:()=>true,$:el,uiErrorText:e=>e.message};
 vm.createContext(context);
 vm.runInContext("var g_aiToken='test-session',g_keyConfigured=false,g_keySaving=null,g_keyRevision=0;"+source,context);
 return {c:context,el};
}
const response=(data,ok=true)=>({ok,status:ok?200:500,json:async()=>data});
const deferred=()=>{let resolve;const promise=new Promise(r=>resolve=r);return {promise,resolve};};
async function run(){
 let posts=0;const oldStatus=deferred(),save=deferred();
 const {c,el}=setup(async(url,options)=>{
  assert.equal(url,'/api/ai/key');assert.equal(options.headers.Authorization,'Bearer test-session');
  if(options.method==='POST'){posts++;assert.equal(JSON.parse(options.body).key,'example-key-123456');return save.promise;}
  return oldStatus.promise;
 });
 const refresh=c.refreshAiKeyStatus();el('apiKeyInput').value='example-key-123456';
 const first=c.saveAiKey(),second=c.saveAiKey();assert.equal(posts,1);assert(el('apiKeyInput').disabled);
 save.resolve(response({ok:true,configured:true}));assert(await first);assert(await second);
 oldStatus.resolve(response({ok:true,configured:false}));await refresh;
 assert(c.g_keyConfigured);assert.equal(el('apiKeyInput').value,'');assert(el('apiKeyStatus').textContent.includes('암호화'));assert(!el('apiKeyInput').disabled);
 const failure=setup(async()=>response({ok:false,error:'NVS 기록 실패'},false));
 failure.el('apiKeyInput').value='example-key-123456';assert.equal(await failure.c.saveAiKey(),false);
 assert.equal(failure.el('apiKeyInput').value,'example-key-123456');assert.equal(failure.el('apiKeyStatus').textContent,'NVS 기록 실패');assert(!failure.el('saveKeyButton').disabled);
 const invalid=setup(()=>{throw Error('Invalid input reached board');});invalid.el('apiKeyInput').value='example key-123456';
 assert.equal(await invalid.c.saveAiKey(),false);assert(invalid.el('apiKeyStatus').textContent.includes('형식 오류'));
 for(const wrapped of [' \n"example-key-123456"\r\n ',"'example-key-123456'",'\u200Bexample-key-123456\u200B','`example-key-123456`']){
  const clean=setup(async(url,options)=>{assert.equal(JSON.parse(options.body).key,'example-key-123456');return response({ok:true,configured:true});});
  clean.el('apiKeyInput').value=wrapped;assert(await clean.c.saveAiKey());
 }
 const gemini=setup(async(url,options)=>{assert.equal(JSON.parse(options.body).key,'AIzaSy_example-key-12345678901234567890123');return response({ok:true,configured:true});});
 gemini.el('apiKeyInput').value='AIzaSy_example-key-12345678901234567890123';assert(await gemini.c.saveAiKey());
 for(const text of ['https://example.com/example-key-123456','example\nkey-123456','example\u200Bkey-123456']){invalid.el('apiKeyInput').value=text;assert.equal(await invalid.c.saveAiKey(),false);}
 invalid.el('apiKeyInput').value='x'.repeat(256);assert.equal(await invalid.c.saveAiKey(),false);assert(invalid.el('apiKeyStatus').textContent.includes('256'));
 console.log('PASS: generated script syntax, authenticated key save, duplicate save, stale status race, failed-save retry, input validation.');
}
run().catch(e=>{console.error(e);process.exitCode=1;});

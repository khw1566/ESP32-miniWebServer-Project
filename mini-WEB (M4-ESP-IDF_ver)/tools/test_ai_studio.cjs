const fs=require('fs'),vm=require('vm'),assert=require('assert'),path=require('path');
class Element {
 constructor(tag){this.tagName=tag;this.children=[];this.attributes={};this.style={};this.value='';this.checked=true;this.disabled=false;this.classList={add(){},remove(){},toggle(){}};}
 appendChild(c){this.children.push(c);return c;} append(...c){this.children.push(...c);}
 setAttribute(k,v){this.attributes[k]=v;} addEventListener(){} focus(){} click(){} remove(){}
 set textContent(x){this.text=String(x);this.children=[];} get textContent(){return (this.text||'')+this.children.map(x=>x.textContent).join('');}
}
const main=path.resolve(__dirname,'../main'),script=fs.readFileSync(path.join(main,'ai_studio.html'),'utf8').match(/<script>([\s\S]*?)<\/script>/g).at(-1).slice(8,-9).replace('/* AI_MEDIA_SCRIPT */',fs.readFileSync(path.join(main,'ai_media.js'),'utf8')).replace('/* AI_ROUTER_SCRIPT */',fs.readFileSync(path.join(main,'ai_router.js'),'utf8')).replace('loadSettings();refreshAiKeyStatus();','');
function setup(fetch){
 const elements={},el=id=>elements[id]||(elements[id]=new Element('div'));
 const context={document:{documentElement:{dataset:{},style:{setProperty(){}}},querySelector:()=>({offsetHeight:40}),getElementById:el,createElement:t=>new Element(t),createElementNS:(ns,t)=>{const e=new Element(t);e.namespaceURI=ns;return e;},createTextNode:t=>{const e=new Element('#text');e.textContent=t;return e;}},
  window:{g_aiToken:'test-session',addEventListener(){}},innerWidth:1000,location:{href:'http://[fd00::1]/ai'},localStorage:{getItem:()=>null,removeItem(){},setItem(){}},sessionStorage:{getItem:()=>null,removeItem(){}},URL,TextEncoder,console,fetch,setTimeout(){},alert:m=>{throw Error(m);}};
 vm.createContext(context);vm.runInContext(script,context);vm.runInContext(`AiRequestRouter.prototype.request=(function(original){return function(contents){this.activate({category:'all'});return original.call(this,contents);};})(AiRequestRouter.prototype.request);`,context);
 context.g_keyConfigured=true;el('modelPreset').value='test-model';el('apiKeyInput').value='';el('adminPassword').value='관리자-secret';el('agentEnabled').checked=true;el('userPrompt').value='기존 파일을 읽고 제목을 바꾸고 메인으로 지정해줘';
 return {context,el};
}
function all(e,tag){return (e.tagName===tag?[e]:[]).concat(e.children.flatMap(x=>all(x,tag)));}
async function run(){
 const {context:c}=setup(()=>{throw Error('Unexpected network');});
 const root=new Element('div');
 c.renderMarkdown(root,'# 큰 제목\n## 소제목\n**굵은 글씨**와 *기울임*\n- 첫 항목\n- 둘째 항목\n\n| 이름 | 값 |\n| --- | --- |\n| A | 12 |\n\n<script>alert(1)</script>\n[위험](javascript:alert)\n\x60\x60\x60html\n<h1>코드</h1>\n\x60\x60\x60');
 for(const tag of ['h1','h2','strong','em','ul','table','pre'])assert(all(root,tag).length,tag+' missing');
 assert.equal(all(root,'script').length,0);assert.equal(all(root,'a').length,0);
 for(const type of ['bar','line','pie']){const box=new Element('div');c.renderChart(box,JSON.stringify({type,labels:['A','B'],values:[3,7]}));assert.equal(all(box,'svg').length,1);assert.equal(all(box,'table').length,1);}
 assert.throws(()=>c.renderChart(root,'{"type":"pie","labels":["A"],"values":[0]}'));
 assert.throws(()=>c.renderChart(root,'{"type":"bar","labels":["A"],"values":[-1]}'));
 let aiCalls=0,operations=[],bodies=[];
 const responses=[
  {role:'model',parts:[{thoughtSignature:'signature-1',functionCall:{id:'read-1',name:'read_file',args:{name:'index.html'}}}]},
  {role:'model',parts:[{thoughtSignature:'signature-2',functionCall:{id:'write-1',name:'write_file',args:{name:'index.html',content:'<h1>새 제목</h1>',expected_version:'v1'}}}]},
  {role:'model',parts:[{thoughtSignature:'signature-3',functionCall:{id:'home-1',name:'set_home',args:{name:'index.html'}}}]},
  {role:'model',parts:[{text:'## 완료\n**메인 화면**을 변경했습니다.'}]}
 ];
 const env=setup(async(url,options)=>{
  if(url==='/api/agent'){
   assert.equal(options.headers.Authorization,'Bearer test-session');assert(!JSON.stringify(options).includes('provider-secret'));
   const b=JSON.parse(options.body);operations.push(b.name);
   return {ok:true,json:async()=>b.name==='read_file'?{ok:true,content:'<h1>기존</h1>',version:'v1'}:{ok:true,bytes:30}};
  }
  assert(url.startsWith('/api/ai/chat?model='));const body=JSON.parse(options.body);bodies.push(body);assert(!options.body.includes('관리자-secret'));assert(!JSON.stringify(options.headers).includes(encodeURIComponent('관리자-secret')));
  return {ok:true,json:async()=>({candidates:[{finishReason:'STOP',content:responses[aiCalls++]}]})};
 });
 await env.context.sendMessage();
 assert.deepEqual(operations,['read_file','write_file','set_home']);assert.equal(aiCalls,4);
 assert.equal(bodies[1].contents[1].parts[0].thoughtSignature,'signature-1');
 assert.equal(bodies[1].contents[2].parts[0].functionResponse.id,'read-1');
 assert.equal(env.el('sendBtn').disabled,false);assert.equal(env.context.g_chatHistory.length,1);
 env.el('agentEnabled').checked=false;await assert.rejects(env.context.agentRequest('list_files',{}));
 const conflict=setup(async(url,options)=>{
  if(url==='/api/agent')return {ok:false,json:async()=>({ok:false,error:'File changed'})};
  const body=JSON.parse(options.body);
  if(body.contents.length===1)return {ok:true,json:async()=>({candidates:[{finishReason:'STOP',content:responses[1]}]})};
  assert.equal(body.contents.at(-1).parts[0].functionResponse.response.ok,false);
  return {ok:true,json:async()=>({candidates:[{finishReason:'STOP',content:{role:'model',parts:[{text:'충돌로 저장하지 못했습니다.'}]}}]})};
 });
 await conflict.context.sendMessage();assert(conflict.el('chatMessages').textContent.includes('실패'));
 // Split files: named fences, bundle apply, Korean 404 treated as a new file.
 const writes=[],alerts=[];
 const split=setup(async(url,options)=>{
  const b=JSON.parse(options.body);
  if(b.name==='read_file')return b.arguments.name==='shop.css'?{ok:true,status:200,json:async()=>({ok:true,content:'old',version:'v7'})}:{ok:false,status:404,json:async()=>({ok:false,error:'파일을 찾을 수 없습니다.'})};
  writes.push(b.name+':'+(b.arguments.name||'')+':'+(b.arguments.expected_version||''));return {ok:true,status:200,json:async()=>({ok:true,bytes:10})};
 });
 split.context.confirm=()=>true;split.context.alert=m=>alerts.push(m);
 const bubble=new Element('div');
 split.context.renderMarkdown(bubble,'\x60\x60\x60html shop.html\n<link rel="stylesheet" href="shop.css"><script src="shop.js" defer></'+'script><h1>가게</h1>\n\x60\x60\x60\n\x60\x60\x60css shop.css\nh1{color:red}\n\x60\x60\x60\n\x60\x60\x60js shop.js\nconsole.log(1)\n\x60\x60\x60');
 assert.deepEqual(bubble._webFiles.map(f=>f.name+'/'+f.kind),['shop.html/html','shop.css/css','shop.js/js']);
 const inlined=split.context.inlineWebFiles(bubble._webFiles[0],bubble._webFiles);
 assert(inlined.includes('<style>h1{color:red}</style>'));assert(inlined.includes('console.log(1)'));assert(!inlined.includes('href="shop.css"'));
 await split.context.applyFilesToBoard(bubble._webFiles,new Element('button'));
 assert.deepEqual(writes,['write_file:shop.css:v7','write_file:shop.js:new','write_file:shop.html:new','set_home:shop.html:','list_files::']);
 const applied=split.el('chatMessages').textContent;assert(applied.includes('메인 화면으로 지정했습니다'));assert(applied.includes('페이지 열기: /shop.html'));assert(!applied.includes('실패'));
 // Real errors still stop before writing anything.
 const broken=setup(async()=>({ok:false,status:500,json:async()=>({ok:false,error:'storage busy'})}));
 const brokenAlerts=[];broken.context.alert=m=>brokenAlerts.push(m);broken.context.confirm=()=>true;
 await broken.context.applyFilesToBoard([{name:'a.html',kind:'html',code:'x'}],new Element('button'));
 const brokenText=broken.el('chatMessages').textContent;assert(brokenText.includes('storage busy'));assert(brokenText.includes('변경되지 않았습니다'));
 // Automatic names: title slug, consistent links, missing links injected, collisions renamed.
 const named=new Element('div');
 split.context.renderMarkdown(named,'\x60\x60\x60html\n<html><head><title>Cafe Menu</title><link rel="stylesheet" href="./style.css"></head><body><h1>메뉴</h1></body></html>\n\x60\x60\x60\n\x60\x60\x60css\nh1{color:red}\n\x60\x60\x60\n\x60\x60\x60js\nconsole.log(2)\n\x60\x60\x60');
 assert.equal(split.context.webBase(named),'cafe-menu');
 const plan=split.context.planPage(named._webFiles,'cafe-menu');
 assert.deepEqual(plan.files.map(f=>f.name),['cafe-menu.css','cafe-menu.js','cafe-menu.html']);
 assert(plan.html.code.includes('href="cafe-menu.css"'));assert(!plan.html.code.includes('style.css'));
 assert(plan.html.code.includes('<script src="cafe-menu.js" defer></'+'script>\n</body>'));
 const korean=new Element('div');split.context.renderMarkdown(korean,'\x60\x60\x60html\n<title>카페</title><h1>카페</h1>\n\x60\x60\x60');
 assert.equal(split.context.webBase(korean),'page');
 const saves=[],taken=['cafe-menu.html','cafe-menu.css'];
 const clash=setup(async(url,options)=>{const b=JSON.parse(options.body);
  if(b.name==='list_files')return {ok:true,status:200,json:async()=>({ok:true,files:taken.map(name=>({name}))})};
  if(b.name==='read_file')return {ok:false,status:404,json:async()=>({ok:false,error:'파일을 찾을 수 없습니다.'})};
  saves.push(b.name+':'+(b.arguments.name||''));if(b.name==='write_file'&&b.arguments.name.endsWith('.html'))assert(b.arguments.content.includes('cafe-menu-2.css'));
  return {ok:true,status:200,json:async()=>({ok:true,bytes:5})};});
 clash.context.confirm=()=>false;clash.context.alert=()=>{};
 await clash.context.applyAnswer(named,new Element('button'));
 assert.deepEqual(saves,['write_file:cafe-menu-2.css','write_file:cafe-menu-2.js','write_file:cafe-menu-2.html']);
 // Conversation persistence, edit target, refinement chips.
 const store={},mem=setup(async()=>({ok:true,status:200,json:async()=>({ok:true,files:[{name:'index.html'},{name:'photo.jpg'},{name:'site.css'}]})}));
 mem.context.localStorage={getItem:k=>store[k]??null,setItem:(k,v)=>{store[k]=v;},removeItem:k=>{delete store[k];}};
 mem.context.appendMessage('user','카페 페이지 만들어줘');mem.context.appendMessage('ai','완성했습니다.');
 mem.context.g_chatHistory=[[{role:'user',parts:[{inlineData:{data:'x'}}]}]];mem.context.saveChat();
 assert(!store.mw_ai_chat.includes('"data"'));
 const again=setup(async()=>{throw Error('no network');});again.context.localStorage=mem.context.localStorage;again.context.restoreChat();
 assert(again.el('chatMessages').textContent.includes('카페 페이지 만들어줘'));assert(again.el('chatMessages').textContent.includes('이전 대화를 불러왔습니다'));
 assert.equal(again.context.g_transcript.length,2);assert.equal(again.context.g_chatHistory.length,1);
 again.context.g_busy=false;again.context.resetChat();assert.equal(store.mw_ai_chat,undefined);
 await mem.context.loadEditTargets(true);
 assert.deepEqual(mem.el('editTarget').children.map(o=>o.value),['','index.html','site.css']);
 mem.el('editTarget').value='index.html';assert(mem.context.webFormatInstruction()!==undefined);
 assert(mem.context.editTargetInstruction().includes('existing board file index.html'));
 mem.el('editTarget').value='';assert.equal(mem.context.editTargetInstruction(),'');
 const refine=new Element('div');refine._webFiles=[{name:'cafe.html',kind:'html',code:'<h1>x</h1>'}];mem.context.answerCard(refine);
 assert(all(refine,'button').some(b=>b.textContent==='모바일 다듬기'));
 // The chosen output format reaches the model instructions.
 split.el('webFormat').value='full';assert(split.context.webFormatInstruction().includes('three files'));
 split.el('webFormat').value='html';assert(split.context.webFormatInstruction().includes('no CSS or JavaScript'));
 const generated=fs.readFileSync(path.join(main,'web_page_ai.c'),'utf8');
 const html=[...generated.slice(generated.indexOf('const char HTML_AI_STUDIO')).matchAll(/"(?:\\.|[^"\\])*"/g)].map(m=>JSON.parse(m[0])).join('');
 assert(html.includes("sandbox='allow-scripts'"));assert(!html.includes('AI_STUDIO_SCRIPT'));
 for(const match of html.matchAll(/<script>([\s\S]*?)<\/script>/g))new vm.Script(match[1]);
 console.log('PASS: embedded script syntax, headings/bold/lists/tables/code, inert HTML/unsafe links, 3 chart types, read/write/home tool loop, thought signatures/call IDs, credential isolation, disabled tools, conflict handling, split web files, 404 as new file, automatic names and links, collision renaming, saved conversation, edit target, refinement chips, output format.');
}
run().catch(e=>{console.error(e);process.exitCode=1;});

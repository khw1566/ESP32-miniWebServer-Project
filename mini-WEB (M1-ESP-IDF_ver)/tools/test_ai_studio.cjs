const fs=require('fs'),vm=require('vm'),assert=require('assert'),path=require('path');
class Element {
 constructor(tag){this.tagName=tag;this.children=[];this.attributes={};this.style={};this.value='';this.checked=true;this.disabled=false;this.classList={add(){},remove(){},toggle(){}};}
 appendChild(c){this.children.push(c);return c;} append(...c){this.children.push(...c);}
 setAttribute(k,v){this.attributes[k]=v;} addEventListener(){} focus(){} click(){} remove(){}
 set textContent(x){this.text=String(x);this.children=[];} get textContent(){return (this.text||'')+this.children.map(x=>x.textContent).join('');}
}
const main=path.resolve(__dirname,'../main'),script=fs.readFileSync(path.join(main,'ai_studio.js'),'utf8');
function setup(fetch){
 const elements={},el=id=>elements[id]||(elements[id]=new Element('div'));
 const context={document:{getElementById:el,createElement:t=>new Element(t),createElementNS:(ns,t)=>{const e=new Element(t);e.namespaceURI=ns;return e;},createTextNode:t=>{const e=new Element('#text');e.textContent=t;return e;}},
  window:{addEventListener(){}},innerWidth:1000,location:{href:'http://[fd00::1]/ai'},localStorage:{getItem:()=>null,removeItem(){},setItem(){}},sessionStorage:{getItem:()=>null},URL,TextEncoder,console,fetch,setTimeout(){},alert:m=>{throw Error(m);}};
 vm.createContext(context);vm.runInContext(script,context);
 el('apiKeyInput').value='provider-secret';el('adminPassword').value='관리자-secret';el('agentEnabled').checked=true;el('userPrompt').value='기존 파일을 읽고 제목을 바꾸고 메인으로 지정해줘';
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
   assert.equal(options.headers['X-Admin-Password'],encodeURIComponent('관리자-secret'));assert(!JSON.stringify(options).includes('provider-secret'));
   const b=JSON.parse(options.body);operations.push(b.name);
   return {ok:true,json:async()=>b.name==='read_file'?{ok:true,content:'<h1>기존</h1>',version:'v1'}:{ok:true,bytes:30}};
  }
  const body=JSON.parse(options.body);bodies.push(body);assert(!options.body.includes('관리자-secret'));assert(!JSON.stringify(options.headers).includes(encodeURIComponent('관리자-secret')));
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
 const generated=fs.readFileSync(path.join(main,'web_page_ai.c'),'utf8');
 const html=[...generated.slice(generated.indexOf('const char HTML_AI_STUDIO')).matchAll(/"(?:\\.|[^"\\])*"/g)].map(m=>JSON.parse(m[0])).join('');
 assert(html.includes("sandbox='allow-scripts'"));assert(!html.includes('AI_STUDIO_SCRIPT'));
 const inline=html.match(/<script>([\s\S]*?)<\/script>/)[1];new vm.Script(inline);
 console.log('PASS: embedded script syntax, headings/bold/lists/tables/code, inert HTML/unsafe links, 3 chart types, read/write/home tool loop, thought signatures/call IDs, credential isolation, disabled tools, conflict handling.');
}
run().catch(e=>{console.error(e);process.exitCode=1;});

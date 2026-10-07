const fs=require('fs'),path=require('path'),vm=require('vm'),assert=require('assert');
const main=path.resolve(__dirname,'../main');
const source=fs.readFileSync(path.join(main,'ai_router.js'),'utf8');
const html=fs.readFileSync(path.join(main,'ai_studio.html'),'utf8');
const script=html.match(/<script>([\s\S]*?)<\/script>/g).at(-1).slice(8,-9)
 .replace('/* AI_ROUTER_SCRIPT */',source)
 .replace('/* AI_MEDIA_SCRIPT */',fs.readFileSync(path.join(main,'ai_media.js'),'utf8'))
 .replace('loadSettings();refreshAiKeyStatus();','');
class Element{
 constructor(tag){this.tagName=tag;this.children=[];this.value='';this.checked=true;this.disabled=false;this.style={};this.classList={add(){},remove(){},toggle(){}};}
 appendChild(child){this.children.push(child);return child;}append(...children){this.children.push(...children);}
 setAttribute(){}addEventListener(){}focus(){}click(){}remove(){}
 set textContent(value){this.text=String(value);this.children=[];}get textContent(){return (this.text||'')+this.children.map(c=>c.textContent).join('');}
}
function setup(fetch){
 const elements={},el=id=>elements[id]||(elements[id]=new Element('div'));
 const ctx={fetch,TextEncoder,URL,console,setTimeout(){},alert:m=>{throw Error(m);},innerWidth:1000,
  window:{g_aiToken:'session',addEventListener(){}},location:{replace(){}},
  document:{documentElement:{dataset:{},style:{setProperty(){}}},querySelector:()=>({offsetHeight:40}),getElementById:el,
   createElement:t=>new Element(t),createTextNode:text=>{const e=new Element('text');e.textContent=text;return e;}}};
 vm.createContext(ctx);vm.runInContext(script,ctx);ctx.g_keyConfigured=true;el('modelPreset').value='test-model';el('agentEnabled').checked=true;
 return {ctx,el};
}
const reply=parts=>({ok:true,status:200,json:async()=>({candidates:[{finishReason:'STOP',content:{role:'model',parts}}]})});
const call=(name,args,id)=>({thoughtSignature:'signature-'+id,functionCall:{name,args,id}});
const plain=text=>({text});
async function run(){
 // Real sendMessage integration: no overridden router methods.
 let greetings=0,initialBytes=0;
 const chat=setup(async(url,options)=>{
  assert(url.startsWith('/api/ai/chat?model='));assert.equal(options.headers.Authorization,'Bearer session');
  const body=JSON.parse(options.body);const names=body.tools[0].functionDeclarations.map(t=>t.name);
  assert.deepEqual(names,['request_board_tools']);assert.equal(body.toolConfig.functionCallingConfig.mode,'AUTO');
  assert(body.system_instruction.parts[0].text.includes('natural Korean prose'));
  initialBytes=new TextEncoder().encode(options.body).length;greetings++;
  return reply([plain('안녕하세요. 웹 파일 관리와 보드 상태 확인을 도와드릴 수 있어요.')]);
 });
 for(const prompt of ['안녕?','자기 소개해주고 네가 뭘 할 수 있는지 알려줘']){
  chat.el('userPrompt').value=prompt;await chat.ctx.sendMessage();
 }
 assert.equal(greetings,2);assert(!chat.el('chatMessages').textContent.includes('요청을 완료하지 못했습니다'));
 assert(!chat.el('chatMessages').textContent.includes('request_board_tools'));
 assert(!chat.el('chatMessages').textContent.includes('이미 완료된'));
 let round=0,boardCalls=[],bodies=[];
 const task=setup(async(url,options)=>{
  const body=JSON.parse(options.body);
  if(url==='/api/agent'){
   boardCalls.push(body.name);assert.equal(body.name,'read_file');assert.equal(body.arguments.name,'index.html');
   return {ok:true,json:async()=>({ok:true,content:'<h1>기존 제목</h1>',version:'v1'})};
  }
  bodies.push(body);
  if(round++===0)return reply([call('request_board_tools',{category:'files'},'route-1')]);
  if(round===2){
   const names=body.tools[0].functionDeclarations.map(t=>t.name);
   assert(names.includes('read_file'));assert(!names.includes('get_diagnostics'));
   assert.equal(body.contents[1].parts[0].thoughtSignature,'signature-route-1');
   assert.equal(body.contents[2].parts[0].functionResponse.id,'route-1');
   assert.equal(body.contents[2].parts[0].functionResponse.response.ok,true);
   return reply([call('read_file',{name:'index.html'},'read-1')]);
  }
  assert.equal(body.contents.at(-1).parts[0].functionResponse.response.version,'v1');
  return reply([plain('현재 메인 파일의 제목은 기존 제목입니다.')]);
 });
 task.el('userPrompt').value='index.html을 읽고 현재 제목을 알려줘';await task.ctx.sendMessage();
 assert.deepEqual(boardCalls,['read_file']);assert.equal(round,3);
 assert(task.el('chatMessages').textContent.includes('현재 메인 파일의 제목은 기존 제목입니다.'));
 assert(!task.el('chatMessages').textContent.includes('지원하지 않는 도구'));
 const routing=vm.runInContext('new AiRequestRouter(tools,instructions,true)',task.ctx);
 assert(!routing.allows('write_file'));routing.activate({category:'diagnostics'});
 assert(routing.allows('get_logs'));assert(!routing.allows('write_file'));
 assert.throws(()=>routing.activate({category:'__proto__'}));
 routing.activate({category:'files'});assert(routing.allows('write_file'));assert(routing.allows('get_logs'));
 // Disabled tools retain normal conversation and cannot be opened by a call.
 const off=setup(async(url,options)=>{
  const b=JSON.parse(options.body);assert.equal(b.tools,undefined);assert.equal(b.toolConfig,undefined);
  return reply([plain('안녕하세요. 편하게 질문해 주세요.')]);
 });
 off.el('agentEnabled').checked=false;off.el('userPrompt').value='안녕';await off.ctx.sendMessage();
 assert(off.el('chatMessages').textContent.includes('편하게 질문해 주세요.'));
 assert.throws(()=>vm.runInContext("new AiRequestRouter(tools,instructions,false).activate({category:'all'})",off.ctx));
 // Keep the active task and drop whole historical turns on the size bound.
 const contents=[{role:'user',parts:[{text:'x'.repeat(50000)}]},{role:'model',parts:[plain('old')]},{role:'user',parts:[plain('안녕')]}];
 const encoded=routing.encode(contents,2);assert.equal(encoded.dropped,2);assert.equal(contents.length,1);
 const active=[{role:'user',parts:[plain('x'.repeat(50000))]}];assert.throws(()=>routing.encode(active,0));assert.equal(active.length,1);
 // A provider response with no visible text must not silently succeed.
 const empty=setup(async()=>reply([{thought:true,text:'internal'}]));empty.el('userPrompt').value='안녕';await empty.ctx.sendMessage();
 assert(empty.el('chatMessages').textContent.includes('답변 없이'));assert(!empty.el('chatMessages').textContent.includes('이미 완료된'));
 assert.equal(empty.el('sendBtn').disabled,false);
 // Execute the actual C structural guard body as a small source harness.
 const parser=fs.readFileSync(path.join(main,'web_ai_request.inc'),'utf8');
 const scan=parser.slice(parser.indexOf('    for (int i = 0; i < done; ++i)'),parser.indexOf('    if (quoted || depth)'))
  .replace('for (int i','for (let i').replace('char c =','let c =')
  .replaceAll('reason = AI_JSON_COMPLEXITY; goto failed;','return false;').replaceAll('goto failed;','return false;');
 const cGuard=vm.runInNewContext(`(function(body){let done=body.length,nodes=1,depth=0,quoted=false,escape=false;${scan}return !quoted&&!depth;})`);
 const clientBudget=vm.runInContext('aiRequestBudget',task.ctx);
 for(const b of bodies){const text=JSON.stringify(b);assert(clientBudget(text).ok);assert(cGuard(text));}
 const many=JSON.stringify(Array.from({length:400},()=>0));assert(!clientBudget(many).ok);assert(!cGuard(many));
 const deep='['.repeat(17)+'0'+']'.repeat(17);assert(!clientBudget(deep).ok);assert(!cGuard(deep));
 const escaped=JSON.stringify({text:'[{}] " \\ \n'});assert(clientBudget(escaped).ok);assert(cGuard(escaped));
 function body(name){const start=parser.indexOf('{',parser.indexOf('static const char *'+name));let depth=1,end=start+1;for(;depth;end++){if(parser[end]==='{')depth++;if(parser[end]==='}')depth--;}return parser.slice(start+1,end-1);}
 const errors=['AI_JSON_OK','AI_JSON_SIZE','AI_JSON_TYPE','AI_JSON_MEMORY','AI_JSON_TIMEOUT','AI_JSON_RECEIVE','AI_JSON_COMPLEXITY','AI_JSON_SYNTAX'];
 const errorContext=Object.fromEntries(errors.map((name,i)=>[name,i]));
 const status=vm.runInNewContext(`(function(error){${body('ai_json_error_status')}})`,errorContext);
 const message=vm.runInNewContext(`(function(error){${body('ai_json_error_message')}})`,errorContext);
 for(const [name,expected] of [['AI_JSON_MEMORY','503'],['AI_JSON_TIMEOUT','408'],['AI_JSON_TYPE','415'],['AI_JSON_SIZE','413'],['AI_JSON_COMPLEXITY','413'],['AI_JSON_SYNTAX','400']])assert(status(errorContext[name]).startsWith(expected));
 assert(message(errorContext.AI_JSON_MEMORY).includes('메모리'));assert(!message(errorContext.AI_JSON_MEMORY).includes('48 KiB'));
 assert(message(errorContext.AI_JSON_TIMEOUT).includes('수신 시간'));assert(message(errorContext.AI_JSON_COMPLEXITY).includes('중첩 깊이'));
 console.log(`PASS: greetings/intro, semantic tool routing, real read/final prose, signatures/IDs, disabled tools, history limits, empty replies and actual C structural guard. Initial chat: ${initialBytes} bytes.`);
}
run().catch(error=>{console.error(error);process.exitCode=1;});

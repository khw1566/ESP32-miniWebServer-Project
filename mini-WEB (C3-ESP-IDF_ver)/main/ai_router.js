/* Semantic routing runs in Gemini; this bridge exposes tools only as needed. */
const AI_ROUTER_GROUPS = {
 files: ['list_files','read_file','write_file','set_home','get_settings'],
 diagnostics: ['get_diagnostics','get_logs'],
 media: ['inspect_media','list_files'],
 settings: ['get_settings']
};
const AI_ROUTER_DECLARATION = {
 name:'request_board_tools',
 description:'Request a board tool group only when live board data or an actual operation is needed. Greetings, introductions, capabilities and general questions need no tools.',
 parameters:{type:'OBJECT',properties:{category:{type:'STRING',enum:['files','diagnostics','media','settings','all']}},required:['category']}
};
const AI_ROUTER_INSTRUCTIONS = 'You are the Mini WebServer assistant. Understand the user intent and answer in natural Korean prose. Greetings, self-introductions, capability questions and general explanations need a direct text answer without tools. You can help with web files, home page selection, board diagnostics and visual media samples. For actual board data or changes, request_board_tools selects files, diagnostics, media or settings; use all only for tasks needing several groups. A tool group selection only enables tools: it does not read or change the board. Use the newly enabled tools to do the requested work, then explain the measured result naturally. Ask a short question if the request is ambiguous. Never claim an operation succeeded without its successful tool response. Never invent board measurements. Prefer plain sentences; use lists, tables, code or charts only when helpful or requested. Never expose routing JSON as the answer. No emoji. If board tools are disabled, answer normal questions and explain that actual board operations require enabling them.';
class AiRequestRouter {
 constructor(declarations,instructions,enabled){
  this.declarations=declarations;this.instructions=instructions;this.enabled=enabled;
  this.active=new Set();
 }
 isRoute(name){return name===AI_ROUTER_DECLARATION.name;}
 allows(name){return this.enabled&&this.active.has(name);}
 activate(args){
  if(!this.enabled)throw Error('보드 도구 사용이 꺼져 있습니다.');
  const category=args?.category;
  const names=category==='all'?this.declarations.map(t=>t.name):(Object.hasOwn(AI_ROUTER_GROUPS,category)?AI_ROUTER_GROUPS[category]:null);
  if(!names)throw Error('지원하지 않는 작업 분류입니다.');
  names.forEach(name=>this.active.add(name));
  return {ok:true,category,available_tools:[...this.active],message:'Tools enabled. No board operation has occurred. Execute the requested tools, then respond to the user in natural Korean.'};
 }
 request(contents){
  const body={system_instruction:{parts:[{text:AI_ROUTER_INSTRUCTIONS+' '+this.instructions}]},contents,generationConfig:{temperature:0.4,maxOutputTokens:8192}};
  if(this.enabled){
   body.tools=[{functionDeclarations:[AI_ROUTER_DECLARATION,...this.declarations.filter(t=>this.active.has(t.name))]}];
   body.toolConfig={functionCallingConfig:{mode:'AUTO'}};
  }
  return body;
 }
 encode(contents,turnStart){
  let dropped=0,body=this.request(contents),payload=JSON.stringify(body),budget=aiRequestBudget(payload);
  // Drop only complete older turns; preserve every step of the current task.
  if(!budget.ok&&turnStart>0){
   contents.splice(0,turnStart);dropped=turnStart;
   body=this.request(contents);payload=JSON.stringify(body);budget=aiRequestBudget(payload);
  }
  if(!budget.ok)throw Error(budget.reason==='bytes'?'대화가 48 KiB를 초과했습니다. 파일이나 요청을 작게 나누세요.':'현재 작업의 데이터 구조가 너무 복잡합니다. 파일이나 진단 요청을 나누어 보내세요.');
  return {payload,dropped};
 }
}
function aiRequestBudget(payload){
 const bytes=new TextEncoder().encode(payload).length;
 if(bytes>48*1024)return {ok:false,reason:'bytes',bytes};
 let nodes=1,depth=0,quoted=false,escaped=false;
 for(const ch of payload){
  if(quoted){if(escaped)escaped=false;else if(ch==='\\')escaped=true;else if(ch==='"')quoted=false;}
  else if(ch==='"')quoted=true;
  else if(ch==='{'||ch==='['){nodes++;if(++depth>16)return {ok:false,reason:'depth',bytes,nodes};}
  else if(ch==='}'||ch===']')depth--;
  else if(ch===':'||ch===',')nodes++;
  if(nodes>384)return {ok:false,reason:'nodes',bytes,nodes};
 }
 return {ok:true,bytes,nodes};
}

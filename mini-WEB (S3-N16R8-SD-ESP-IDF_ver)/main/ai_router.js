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
/* Intent routing in the browser: picks the instruction set before the first
 * request and pre-enables the likely tool group. The model can still request
 * other groups through request_board_tools. */
const AI_INTENT_LABELS={general:'일반 질문',web:'웹 페이지 작업',board:'보드 관리'};
const AI_INTENT_INSTRUCTIONS={
 general:' Route: general conversation or question. Answer directly and concisely in natural Korean prose. Use board tools only if live board data is truly needed. Do not create web pages, files or long code unless the user explicitly asks for them.',
 board:' Route: board management. Use the enabled tools to get real data first, then summarize the findings in plain Korean, with a short table only when it helps. Do not create web pages unless asked.'
};
function aiClassifyIntent(prompt,context){
 const p=String(prompt||'').toLowerCase();
 if(context&&context.editTarget)return {intent:'web',group:'files'};
 const make=/(만들|생성|제작|작성해|수정|고쳐|바꿔|변경|추가|넣어|빼 ?줘|지워|꾸며|디자인|다듬|보강|개선|리뉴얼)/.test(p);
 const page=/(페이지|웹사이트|사이트|홈페이지|랜딩|html|css|자바스크립트|javascript|\bjs\b|레이아웃|섹션|포트폴리오|메뉴판|웹 ?화면)/.test(p);
 if(make&&page)return {intent:'web',group:'files'};
 const knowledge=/(뭐야|무엇|뭔가요|뭔지|의미|차이|정의)/.test(p);
 if(/(이미지|사진|영상|동영상|미디어|mp4|jpe?g|png|webp)/.test(p)&&/(분석|봐 ?줘|확인|설명|살펴)/.test(p))return {intent:'board',group:'media'};
 if(!knowledge&&/(진단|로그|메모리|ram|힙|heap|와이 ?파이|wi-?fi|\bip\b|주소|신호|rssi|오류|에러|재부팅|리셋|느려|끊)/.test(p))return {intent:'board',group:'diagnostics'};
 if(!knowledge&&/(파일|목록|메인 ?화면|메인으로|용량|저장 ?공간|스토리지|\.html?\b|\.css\b|\.js\b)/.test(p))return {intent:'board',group:'files'};
 return {intent:'general',group:null};
}
function aiJsonNodes(value){
 if(Array.isArray(value))return value.reduce((n,v)=>n+1+aiJsonNodes(v),1);
 if(value&&typeof value==='object')return Object.values(value).reduce((n,v)=>n+1+aiJsonNodes(v),1);
 return 0;
}
/* The board accepts at most 384 JSON items per request: lists go to the model as text. */
function aiCompactToolResult(name,result){
 if(!result||typeof result!=='object')return result;
 if(name==='list_files'&&Array.isArray(result.files)){
  const {files,...rest}=result;
  const rows=files.map(f=>[f.name,f.bytes+' bytes',f.editable?'editable':'read-only',f.modified_at?new Date(f.modified_at).toISOString().slice(0,16).replace('T',' '):''].join(' | '));
  return {...rest,count:files.length,files_table:'name | size | edit | modified (UTC)\n'+rows.join('\n')};
 }
 if(aiJsonNodes(result)>80){const {ok,error,...rest}=result;return {ok,error,data_json:JSON.stringify(rest)};}
 return result;
}
function aiFlattenToolResults(contents){
 for(const c of contents)for(const part of c.parts||[])if(part.functionResponse){
  const r=part.functionResponse.response;if(r&&typeof r==='object'&&!('data_json' in r)&&aiJsonNodes(r)>4)part.functionResponse.response={ok:r.ok!==false,data_json:JSON.stringify(r)};
 }
}
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
  const body={system_instruction:{parts:[{text:AI_ROUTER_INSTRUCTIONS+' '+this.instructions}]},contents,generationConfig:{temperature:0.6,maxOutputTokens:16384}};
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
  // Last resort: tool results as single strings keep the item count low.
  if(!budget.ok&&budget.reason!=='bytes'){aiFlattenToolResults(contents);body=this.request(contents);payload=JSON.stringify(body);budget=aiRequestBudget(payload);}
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

const fs=require('fs'),path=require('path'),vm=require('vm'),assert=require('assert');
const main=path.resolve(__dirname,'../main');
const media=fs.readFileSync(path.join(main,'ai_media.js'),'utf8');
const script=fs.readFileSync(path.join(main,'ai_studio.html'),'utf8').match(/<script>([\s\S]*?)<\/script>/g).at(-1).slice(8,-9).replace('/* AI_MEDIA_SCRIPT */',media).replace('/* AI_ROUTER_SCRIPT */',fs.readFileSync(path.join(main,'ai_router.js'),'utf8')).replace('loadSettings();refreshAiKeyStatus();','');
class Element{
 constructor(tag){this.tagName=tag;this.children=[];this.value='';this.checked=true;this.style={};this.events={};this.classList={add(){},remove(){},toggle(){}};this.duration=100;this.videoWidth=1280;this.videoHeight=720;this.naturalWidth=1000;this.naturalHeight=750;this.time=0;}
 appendChild(e){this.children.push(e);return e;}append(...e){this.children.push(...e);}setAttribute(){}focus(){}
 set textContent(x){this.text=String(x);this.children=[];}get textContent(){return (this.text||'')+this.children.map(e=>e.textContent).join('');}
 addEventListener(name,fn){this.events[name]=fn;}removeEventListener(name){delete this.events[name];}emit(name){this.events[name]?.();}
 set src(v){this.source=v;if(this.tagName==='image')queueMicrotask(()=>this.emit('load'));}removeAttribute(){}
 load(){queueMicrotask(()=>this.emit('loadeddata'));}set currentTime(v){this.time=v;queueMicrotask(()=>this.emit('seeked'));}get currentTime(){return this.time;}
 getContext(){return {drawImage(){}};}toDataURL(){return 'data:image/jpeg;base64,'+'A'.repeat(this.width>300?20000:4000);}
}
function setup(fetch){
 const elements={},el=id=>elements[id]||(elements[id]=new Element('div'));let revoked=0;
 const c={fetch,Blob,AbortController,TextEncoder,URL:{createObjectURL:()=> 'blob:test',revokeObjectURL(){revoked++;}},Image:class extends Element{constructor(){super('image');}},queueMicrotask,setTimeout,clearTimeout,innerWidth:1000,window:{g_aiToken:'session',addEventListener(){}},location:{replace(){}},document:{documentElement:{dataset:{},style:{setProperty(){}}},querySelector:()=>({offsetHeight:40}),getElementById:el,createElement:t=>new Element(t),createTextNode:t=>{const e=new Element('text');e.textContent=t;return e;}},alert:m=>{throw Error(m);}};
 vm.createContext(c);vm.runInContext(script,c);vm.runInContext(`AiRequestRouter.prototype.request=(function(original){return function(contents){this.activate({category:'all'});return original.call(this,contents);};})(AiRequestRouter.prototype.request);`,c);c.g_keyConfigured=true;el('modelPreset').value='test-model';el('userPrompt').value='이미지와 영상 및 보드를 진단해줘';
 return {c,el,revoked:()=>revoked};
}
const json=data=>({ok:true,status:200,json:async()=>data});
function binary(size){let done=false;return {ok:true,status:200,body:{getReader:()=>({read:async()=>done?{done:true}:(done=true,{done:false,value:new Uint8Array(size)}),cancel:async()=>{},releaseLock(){}})}};}
async function run(){
 let chats=0;const operations=[];
 const env=setup(async(url,options)=>{
  assert.equal(options.headers.Authorization,'Bearer session');
  assert(!JSON.stringify(options).includes('api-key-secret'));
  if(url==='/photo.jpg'||url==='/clip.mp4')return binary(20);
  if(url==='/api/agent'){const b=JSON.parse(options.body);operations.push(b.name);return json(b.name==='inspect_media'?{ok:true,name:b.arguments.name,bytes:20,video:b.arguments.name.endsWith('.mp4')}:{ok:true,snapshot:'measured'});}
  assert(url.startsWith('/api/ai/chat?model='));const body=JSON.parse(options.body);
  if(chats++===0)return json({candidates:[{finishReason:'STOP',content:{role:'model',parts:[{thoughtSignature:'keep',functionCall:{id:'image',name:'inspect_media',args:{name:'photo.jpg'}}},{functionCall:{id:'video',name:'inspect_media',args:{name:'clip.mp4',timestamps:[5,50,90]}}},{functionCall:{name:'get_diagnostics',args:{}}},{functionCall:{name:'get_logs',args:{}}}]}}]});
  const parts=body.contents.at(-1).parts;
  assert.equal(parts.filter(p=>p.inlineData).length,4);assert(parts.filter(p=>p.inlineData).every(p=>p.inlineData.mimeType==='image/jpeg'&&p.inlineData.data.length<=16000));
  assert.equal(parts[0].functionResponse.id,'image');assert(!JSON.stringify(parts[0]).includes('_mediaParts'));assert(JSON.stringify(parts).includes('sampled_frames_no_audio'));assert.equal(body.contents[1].parts[0].thoughtSignature,'keep');
  return json({candidates:[{finishReason:'STOP',content:{role:'model',parts:[{text:'표본 화면과 진단을 확인했습니다.'}]}}]});
 });
 await env.c.sendMessage();assert.equal(chats,2);assert.deepEqual(operations,['inspect_media','inspect_media','get_diagnostics','get_logs']);assert.equal(env.revoked(),2);assert(!env.el('sendBtn').disabled);
 assert.throws(()=>env.c.mediaTimes(100,[-1]));assert.throws(()=>env.c.mediaTimes(100,[100]));assert.throws(()=>env.c.mediaTimes(100,[1,2,3,4]));assert.deepEqual(Array.from(env.c.mediaTimes(100)),[10,50,90]);
 await assert.rejects(env.c.prepareBoardMedia({name:'../photo.jpg',bytes:20}),/정보/);
 await assert.rejects(env.c.prepareBoardMedia({name:'photo.jpg',bytes:9000000}),/정보/);
 await assert.rejects(env.c.prepareBoardMedia({name:'photo.jpg',bytes:21}),/변경/);
 env.el('agentEnabled').checked=false;await assert.rejects(env.c.agentRequest('get_diagnostics',{}),/꺼져/);
 const oversized=setup(async()=>binary(8*1024*1024+1));await assert.rejects(oversized.c.prepareBoardMedia({name:'photo.jpg',bytes:20}),/제한/);
 console.log('PASS: authenticated image/video decoding, frame timestamps, actual inline image tool loop, thought signatures, diagnostics/log tools, size/path bounds, changed file, cleanup and disabled tools.');
}
run().catch(e=>{console.error(e);process.exitCode=1;});

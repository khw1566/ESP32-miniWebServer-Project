import test from 'node:test';
import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import worker,{RelayDevice,mediaName} from '../worker.mjs';
const id='m1-aabbccddeeff',deviceKey='a'.repeat(64),ownerKey='b'.repeat(64);
const hash=s=>createHash('sha256').update(s).digest('hex');
function setup(){
 const env={PUBLIC_DOMAIN:'audeaming.com',DEMO_DEVICE:id,DEVICE_REGISTRY:JSON.stringify({[id]:{tokenHash:hash(deviceKey),uploadTokenHash:hash(ownerKey)}})};
 const device=new RelayDevice({},env);device.pollMs=3000;device.requestMs=2000;
 env.DEVICES={idFromName:x=>x,get:()=>device};
 const board=(path,body)=>worker.fetch(new Request(`https://relay.audeaming.com/v1/devices/${id}/${path}`,{method:body?'POST':'GET',headers:{Authorization:'Bearer '+deviceKey},body:body?JSON.stringify(body):undefined}),env);
 const owner=(action,body,headers={})=>worker.fetch(new Request('https://demo.audeaming.com/__upload/'+action,{method:'POST',headers:{Authorization:'Bearer '+ownerKey,...headers},body:body instanceof Uint8Array?body:JSON.stringify(body)}),env);
 const round=async(action,body,headers,ack)=>{
  const poll=board('poll');while(!device.poll)await new Promise(r=>setTimeout(r,1));
  const pending=owner(action,body,headers);const response=await poll;assert.equal(response.status,200);
  const command=await response.json();assert.equal(command.kind,'upload');assert.equal(command.action,action);
  const result=typeof ack==='function'?ack(command):ack;
  assert.equal((await board(`result/${command.id}/ack`,result)).status,204);
  return{command,response:await pending};
 };
 return{env,device,board,owner,round};
}
test('media filenames reject executable content and traversal',()=>{
 for(const name of ['photo.jpg','battle-video.mp4','oral-history.mp3','music.m4a'])assert.ok(mediaName(name));
 for(const name of ['index.html','site.js','.x.jpg','../x.mp4','folder/x.mp3','x..jpg','한글.jpg'])assert.equal(mediaName(name),false);
});
test('upload requires owner key, not the board connection token',async()=>{
 const s=setup();s.device.lastSeen=Date.now();
 assert.equal((await s.owner('begin',{}, {Authorization:'Bearer '+deviceKey})).status,401);
 assert.equal((await s.owner('begin',{}, {Authorization:''})).status,401);
});
test('binary chunks, board confirmations and commit preserve bytes',async()=>{
 const s=setup(),bytes=Uint8Array.of(0,255,128,65,13,10);
 let stored=[];
 const start=await s.round('begin',{name:'archive.mp3',size:bytes.length,sha256:hash(bytes)}, {},{status:200,message:'started',offset:0});
 const session=start.response.headers.get('x-upload-session');assert.ok(session);
 assert.equal(start.command.sha256,hash(bytes));
 const part=await s.round('chunk',bytes,{'X-Upload-Session':session,'X-Upload-Offset':'0'},cmd=>{
  stored=Buffer.from(cmd.data,'base64');assert.equal(cmd.offset,0);return{status:200,message:'saved',offset:stored.length};
 });
 assert.deepEqual(stored,Buffer.from(bytes));assert.equal((await part.response.json()).offset,bytes.length);
 assert.equal((await s.owner('chunk',bytes,{'X-Upload-Session':session,'X-Upload-Offset':'0'})).status,409);
 const end=await s.round('commit',{}, {'X-Upload-Session':session},{status:200,message:'complete',offset:bytes.length});
 assert.equal(end.response.status,200);assert.equal(s.device.uploadSession,null);assert.equal(s.device.jobs.size,0);
});
test('SD unavailable and storage failures reach owner without false success',async()=>{
 const s=setup();const result=await s.round('begin',{name:'photo.jpg',size:1,sha256:hash('a')},{},{status:503,message:'SD card is not mounted',offset:0});
 assert.equal(result.response.status,503);assert.equal(s.device.uploadSession,null);
});
test('session mismatch, incomplete commit and cancel',async()=>{
 const s=setup();const start=await s.round('begin',{name:'photo.jpg',size:10,sha256:hash('0123456789')},{},{status:200,message:'started',offset:0});
 const session=start.response.headers.get('x-upload-session');
 assert.equal((await s.owner('chunk',Uint8Array.of(1),{'X-Upload-Session':'wrong','X-Upload-Offset':'0'})).status,409);
 assert.equal((await s.owner('commit',{}, {'X-Upload-Session':session})).status,409);
 const abort=await s.round('abort',{}, {'X-Upload-Session':session},{status:200,message:'cancelled',offset:0});
 assert.equal(abort.response.status,200);assert.equal(s.device.uploadSession,null);
});
test('limits, unknown commands and offline board reject before allocating jobs',async()=>{
 const s=setup();assert.equal((await s.owner('begin',{})).status,503);s.device.lastSeen=Date.now();
 assert.equal((await s.owner('begin',{name:'video.mp4',size:8388609,sha256:'a'.repeat(64)})).status,400);
 assert.equal((await s.owner('begin',{name:'page.html',size:1,sha256:'a'.repeat(64)})).status,400);
 assert.equal((await s.owner('delete',{})).status,404);assert.equal(s.device.jobs.size,0);
});
test('parallel uploads are blocked while one board command is pending',async()=>{
 const s=setup();const poll=s.board('poll');while(!s.device.poll)await new Promise(r=>setTimeout(r,1));
 const first=s.owner('begin',{name:'one.jpg',size:1,sha256:hash('a')});const cmd=await (await poll).json();
 assert.equal((await s.owner('begin',{name:'two.jpg',size:1,sha256:hash('b')})).status,409);
 await s.board(`result/${cmd.id}/ack`,{status:503,message:'No card',offset:0});
 assert.equal((await first).status,503);assert.equal(s.device.jobs.size,0);
});

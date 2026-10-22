// Opt-in live MP3 preparation/transfer check. The local token is never printed.
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { validVideoId } from './lib.mjs';
if(!process.argv.includes('--live')){console.error('Use --live --id VIDEO_ID');process.exit(2);}
const root=path.dirname(fileURLToPath(import.meta.url)),ids=process.argv.filter((v,i,a)=>a[i-1]==='--id');
if(!ids.length||ids.some(id=>!validVideoId(id))){console.error('Valid song IDs are required');process.exit(2);}
try{
  const config=JSON.parse(fs.readFileSync(path.join(root,'config.json'),'utf8'));
  const base=`http://127.0.0.1:${Number(config.port)||8790}`;
  const request=async (suffix,method='GET')=>{
    const r=await fetch(base+suffix,{method,headers:{'x-m3-token':config.token},signal:AbortSignal.timeout(15000)});
    if(!r.ok)throw Error('HTTP '+r.status);return r;
  };
  const service=await (await request('/api/status')).json();if(service.music_cache!==1)throw Error('Update relay');
  const results=[];
  for(const id of ids){
    const began=performance.now();const {job}=await (await request('/api/music?id='+id,'POST')).json();
    let state;
    do{
      state=await (await request('/api/music/status?job='+job)).json();
      if(state.state!=='preparing')break;
      await new Promise(r=>setTimeout(r,500));
    }while(performance.now()-began<330000);
    if(state.state!=='ready'){results.push({id,state:state.state});console.log(id+': '+state.state);continue;}
    const prepared=performance.now();
    const response=await request('/api/music/file?job='+job),reader=response.body.getReader();let bytes=0,mp3=false;
    for(;;){const {value,done}=await reader.read();if(done)break;if(!bytes)mp3=value[0]===255&&(value[1]&224)===224;bytes+=value.length;}
    if(bytes!==state.bytes||!mp3||state.rate!==32000||state.channels!==2||state.bitrate!==64000)throw Error('Invalid MP3');
    const again=performance.now(),repeat=await (await request('/api/music?id='+id,'POST')).json();
    if((await (await request('/api/music/status?job='+repeat.job)).json()).state!=='ready')throw Error('Cache reuse failed');
    const result={id,state:'ready',bytes,duration:state.duration,prepare_s:+((prepared-began)/1000).toFixed(3),repeat_ms:+(performance.now()-again).toFixed(1)};
    results.push(result);console.log(JSON.stringify(result));
  }
  fs.mkdirSync(path.join(root,'cache'),{recursive:true});
  fs.writeFileSync(path.join(root,'cache/live-music-verification.json'),JSON.stringify({evidence:'PC_live_API_only_not_firmware_or_board',results},null,2));
  if(results.some(r=>r.state!=='ready'))process.exitCode=1;
}catch{console.error('Live music verification failed; check relay manager/logs. No credentials printed.');process.exitCode=1;}

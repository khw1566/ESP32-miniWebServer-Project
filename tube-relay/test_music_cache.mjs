import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { MusicCache } from './music_cache.mjs';
const dir=fs.mkdtempSync(path.join(os.tmpdir(),'m3 music cache '));
const id='aaaaaaaaaaa',other='bbbbbbbbbbb'; let calls=0;
try {
  const cache=new MusicCache(dir,async (song,file,signal)=>{
    calls++; await new Promise((resolve,reject)=>{const timer=setTimeout(resolve,15);signal.addEventListener('abort',()=>{clearTimeout(timer);reject(Error('cancelled'));},{once:true});});
    fs.writeFileSync(file,Buffer.alloc(256,song.charCodeAt(0)));
    return {title:'日本語 노래',channel:'Fixture',duration:60,rate:32000,channels:2,bitrate:64000};
  },{maxFiles:2,maxBytes:1024});
  const first=cache.start(id);assert.equal(cache.start(id),first,'same preparing song shares one job');
  const second=cache.start(other);assert.equal(cache.start('ccccccccccc'),null,'two bounded producers');
  assert(!cache.file(first),'partial songs cannot be served');
  await Promise.all([cache.jobs.get(first).promise,cache.jobs.get(second).promise]);
  assert.equal(cache.status(first).state,'ready');assert.equal(cache.status(first).bytes,256);assert.equal(cache.status(first).title,'日本語 노래');
  assert.equal(calls,2);assert.equal((await cache.get(id)).title,'日本語 노래');assert.equal(calls,2,'completed MP3 is reused');
  const third=cache.start('ccccccccccc');cache.cancel(third);await cache.jobs.get(third).promise;
  assert.equal(cache.status(third).state,'cancelled');assert(!cache.file(third));assert(!fs.existsSync(path.join(dir,'ccccccccccc.mp3.part')));
  await cache.get('ddddddddddd');assert(fs.readdirSync(dir).filter(n=>n.endsWith('.mp3')).length<=2);
  assert.throws(()=>cache.start('../key.json'));
  const failure=new MusicCache(path.join(dir,'failure'),async (_id,file)=>{fs.writeFileSync(file,'partial');throw Error('upstream denied');});
  const bad=failure.start(id);await failure.jobs.get(bad).promise;assert.equal(failure.status(bad).state,'error');
  assert.equal(failure.status(bad).error,'upstream denied');assert(!failure.file(bad));assert(!fs.existsSync(path.join(dir,'failure',`${id}.mp3.part`)));
  const malformed=new MusicCache(path.join(dir,'invalid'),async (_id,file)=>{fs.writeFileSync(file,Buffer.alloc(200));return {duration:3600};});
  const invalid=malformed.start(id);await malformed.jobs.get(invalid).promise;assert.equal(malformed.status(invalid).state,'error');assert(!malformed.file(invalid));
  console.log('PASS: compressed music cache deduplication, bounded parallel jobs/eviction, persistent metadata, cancellation, invalid media and failure cleanup.');
} finally { fs.rmSync(dir,{recursive:true,force:true}); }

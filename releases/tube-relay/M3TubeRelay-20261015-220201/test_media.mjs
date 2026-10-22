// Loopback integration test with real FFmpeg and synthetic video, no YouTube account/token.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import http from 'node:http';
import { spawn, execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { parsePackets, PACKET } from './lib.mjs';

const root = fs.mkdtempSync(path.join(os.tmpdir(), 'm3 media test '));
const ffmpeg = process.env.M3_TEST_FFMPEG || 'ffmpeg';
const ffprobe = process.env.M3_TEST_FFPROBE || (path.isAbsolute(ffmpeg) ? path.join(path.dirname(ffmpeg), process.platform === 'win32' ? 'ffprobe.exe' : 'ffprobe') : 'ffprobe');
const token = 'loopback-test-token-no-user-credentials';
let child, origin;
const delay = ms => new Promise(r => setTimeout(r, ms));
async function freePort() { const s = http.createServer(); await new Promise(r => s.listen(0, '127.0.0.1', r)); const p = s.address().port; await new Promise(r => s.close(r)); return p; }
try {
  const source = path.join(root, 'source.mp4');
  execFileSync(ffmpeg, ['-hide_banner', '-loglevel', 'error', '-nostdin', '-f', 'lavfi', '-i', 'testsrc2=size=480x272:rate=15',
    '-f', 'lavfi', '-i', 'sine=frequency=440:sample_rate=44100', '-t', '3', '-c:v', 'libx264', '-profile:v', 'baseline',
    '-pix_fmt', 'yuv420p', '-c:a', 'aac', source], { windowsHide: true });
  const data = fs.readFileSync(source);
  origin = http.createServer((req, res) => {
    const match = /bytes=(\d+)-(\d+)/.exec(req.headers.range || '');
    const start = match ? Number(match[1]) : 0, end = match ? Math.min(Number(match[2]), data.length - 1) : data.length - 1;
    res.writeHead(match ? 206 : 200, { 'Content-Length': end - start + 1, 'Content-Type': 'video/mp4',
      ...(match ? { 'Content-Range': `bytes ${start}-${end}/${data.length}` } : {}) }); res.end(data.subarray(start, end + 1));
  });
  await new Promise(r => origin.listen(0, '127.0.0.1', r));
  const info = { id: 'aaaaaaaaaaa', title: '夜に駆ける 合成テスト', channel: '日本語Fixture', duration: 3,
    url: `http://127.0.0.1:${origin.address().port}/source.mp4`, filesize: data.length, vcodec: 'avc1', acodec: 'aac' };
  const stub = path.join(root, 'yt fixture.cjs');
  const queryFile = path.join(root, 'query.txt');
  fs.writeFileSync(stub, `const fs=require('fs');const info=${JSON.stringify(info)};const search=process.argv.find(a=>a.startsWith('ytsearch'));
    if(search)fs.writeFileSync(${JSON.stringify(queryFile)},search.split(':').slice(1).join(':'),'utf8');
    const entry={...info,ie_key:'Youtube',url:'https://www.youtube.com/watch?v='+info.id};
    console.log(process.argv.includes('--version')?'fixture':JSON.stringify(search?{entries:[entry],title:'日本語検索'}:info));`);
  const configFile = path.join(root, 'config.json'), port = await freePort(), mediaPort = await freePort();
  fs.writeFileSync(configFile, JSON.stringify({ host: '127.0.0.1', port, mediaPort, token, ffmpeg, ytdlp: [process.execPath, stub] }));
  child = spawn(process.execPath, [fileURLToPath(new URL('./server.mjs', import.meta.url))], {
    env: { ...process.env, M3_TUBE_CONFIG: configFile, M3_TUBE_CACHE: path.join(root, 'cache'), M3_TUBE_TOKEN: token },
    stdio: ['ignore', 'pipe', 'pipe'], windowsHide: true,
  });
  let logs = ''; child.stdout.on('data', d => { logs += d; }); child.stderr.on('data', d => { logs += d; });
  const base = `http://127.0.0.1:${port}`;
  const request = (url, options = {}) => fetch(base + url, { ...options, headers: { 'x-m3-token': token, ...options.headers }, signal: AbortSignal.timeout(15000) });
  let ready = false;
  for (let i = 0; i < 100; ++i) {
    try { ready = (await (await request('/api/status')).json()).ok; } catch { }
    if (ready) break; if (child.exitCode !== null) throw Error(logs); await delay(100);
  }
  assert(ready, logs);
  assert.equal((await request('/api/radio?station=not-a-station&profile=economy')).status,400);
  assert.equal((await request('/api/radio?station=kbs-classic&profile=constructor')).status,400);
  assert.equal((await request('/api/radio?station=kbs-classic&profile=economy',{headers:{'x-m3-token':'invalid-fixture-token'}})).status,401);
  const query='日本語の歌 YOASOBI';
  for(const music of [false,true]) {
    const response=await request('/api/search?q='+encodeURIComponent(query)+(music?'&music=1':''));
    assert.equal(response.status,200);const list=await response.json();assert.equal(list.items.length,1);
    assert.equal(list.items[0].title,info.title);assert.equal(list.items[0].channel,info.channel);
    assert.equal(fs.readFileSync(queryFile,'utf8'),query,'Japanese reaches yt-dlp unchanged');
  }
  const thumbDir=path.join(root,'cache','thumb');fs.mkdirSync(thumbDir,{recursive:true});
  const fixtureThumb=Buffer.alloc(160*90*2,0x57);fs.writeFileSync(path.join(thumbDir,'aaaaaaaaaaa_160x90.rgb565'),fixtureThumb);
  const thumb=await request('/api/thumb?id=aaaaaaaaaaa&w=160&h=90');assert.equal(thumb.status,200);
  assert.deepEqual(Buffer.from(await thumb.arrayBuffer()),fixtureThumb);
  const song=await request('/api/play?id=aaaaaaaaaaa&audio=1');assert.equal(song.status,200);
  const songWire=parsePackets(Buffer.from(await song.arrayBuffer()));assert.equal(songWire.rest.length,0);
  const songMeta=JSON.parse(songWire.packets.find(p=>p.type===PACKET.INFO).payload);
  assert.equal(songMeta.video,false);assert.equal(songMeta.audio,true);assert.equal(songMeta.rate,32000);assert.equal(songMeta.ch,1);
  assert(!songWire.packets.some(p=>p.type===PACKET.VIDEO));assert(songWire.packets.some(p=>p.type===PACKET.AUDIO));
  assert.equal(JSON.parse(songWire.packets.find(p=>p.type===PACKET.END).payload).reason,'eof');
  assert.equal((await fetch(base + '/api/status')).status, 401);
  assert.equal((await request('/api/play?id=aaaaaaaaaaa&quality=invalid')).status, 400);
  const cancelled = await (await request('/api/download?id=bbbbbbbbbbb&quality=low', { method: 'POST' })).json();
  assert(cancelled.job);
  assert.equal((await request(`/api/download/status?job=${cancelled.job}`, { method: 'DELETE' })).status, 200);
  let cancellation;
  for (let i = 0; i < 100; ++i) { cancellation = await (await request(`/api/download/status?job=${cancelled.job}`)).json(); if (cancellation.state !== 'preparing') break; await delay(50); }
  assert.equal(cancellation.state, 'cancelled', logs);
  assert.equal((await request(`/api/download/file?job=${cancelled.job}`)).status, 409);
  const sizes = {};
  for (const quality of ['low', 'standard', 'high']) {
    const response = await request(`/api/play?id=aaaaaaaaaaa&quality=${quality}`);
    assert.equal(response.status, 200);
    const wire = Buffer.from(await response.arrayBuffer()), { packets, rest } = parsePackets(wire);
    assert.equal(rest.length, 0);
    const metadata = JSON.parse(packets.find(p => p.type === PACKET.INFO).payload);
    assert.equal(metadata.w, 480); assert.equal(metadata.h, 272);
    assert(packets.some(p => p.type === PACKET.VIDEO)); assert(packets.some(p => p.type === PACKET.AUDIO));
    assert.equal(JSON.parse(packets.find(p => p.type === PACKET.END).payload).reason, 'eof');
    sizes[quality] = wire.length;
    const create = await request(`/api/download?id=aaaaaaaaaaa&quality=${quality}`, { method: 'POST' });
    assert.equal(create.status, 202); const { job } = await create.json();
    let status;
    for (let i = 0; i < 100; ++i) { status = await (await request(`/api/download/status?job=${job}`)).json(); if (status.state !== 'preparing') break; await delay(100); }
    assert.equal(status.state, 'ready', `${JSON.stringify(status)} ${logs}`);
    const video = await request(`/api/download/file?job=${job}`);
    assert.equal(video.status, 200); const mp4 = Buffer.from(await video.arrayBuffer());
    assert.equal(mp4.length, status.bytes); assert.equal(mp4.toString('ascii', 4, 8), 'ftyp');
    const output = path.join(root, `${quality}.mp4`); fs.writeFileSync(output, mp4);
    const probe = JSON.parse(execFileSync(ffprobe, ['-v', 'error', '-show_streams', '-of', 'json', output], { windowsHide: true }));
    const v = probe.streams.find(s => s.codec_type === 'video'), a = probe.streams.find(s => s.codec_type === 'audio');
    assert.equal(v.codec_name, 'h264'); assert(/Baseline/.test(v.profile)); assert.equal(v.pix_fmt, 'yuv420p');
    assert(v.width <= 480 && v.height <= 272 && v.width % 16 === 0 && v.height % 16 === 0);
    assert.equal(a.codec_name, 'aac'); assert.equal(a.profile, 'LC'); assert.equal(a.sample_rate, '44100');
  }
  assert(sizes.low < sizes.standard && sizes.standard < sizes.high, JSON.stringify(sizes));
  console.log(`PASS: Japanese video/music search round-trip, thumbnail cache, 32kHz audio-only playback, real FFmpeg video/MP4 at all qualities, authentication and invalid profile rejection. Synthetic video bytes: ${JSON.stringify(sizes)}.`);
} finally {
  if (child && child.exitCode === null) { child.kill(); await new Promise(r => child.once('exit', r)); }
  if (origin) await new Promise(r => origin.close(r));
  fs.rmSync(root, { recursive: true, force: true });
}

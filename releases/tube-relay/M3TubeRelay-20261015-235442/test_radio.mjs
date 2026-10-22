// Real FFmpeg against synthetic local audio. No station accounts or user credentials.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import http from 'node:http';
import { execFileSync } from 'node:child_process';
import { RadioRelay, radioProfiles, radioStations, resolveRadio } from './radio.mjs';

const ffmpeg = process.env.M3_TEST_FFMPEG || 'ffmpeg';
const ffprobe = process.env.M3_TEST_FFPROBE || (path.isAbsolute(ffmpeg) ? path.join(path.dirname(ffmpeg), 'ffprobe.exe') : 'ffprobe');
const root = fs.mkdtempSync(path.join(os.tmpdir(), 'm3-radio-'));
let origin, server;
try {
  assert.equal(radioStations.length, 20);
  assert(radioStations.filter(s => s.region === 1).length >= 6);
  assert(radioStations.filter(s => s.region === 2).length >= 7);
  assert(radioStations.some(s => s.id === 'kbs-classic'));
  const wav = path.join(root, 'source.wav');
  execFileSync(ffmpeg, ['-v', 'error', '-f', 'lavfi', '-i', 'sine=frequency=440:duration=6', '-ar', '48000', '-ac', '2', wav], { windowsHide: true });
  const source = fs.readFileSync(wav);
  origin = http.createServer((req, res) => { res.writeHead(200, { 'Content-Type': 'audio/wav', 'Content-Length': source.length }); res.end(source); });
  await new Promise(r => origin.listen(0, '127.0.0.1', r));
  const station = { id: 'fixture', url: `http://127.0.0.1:${origin.address().port}/source.wav` };
  radioStations.push(station);
  let fetched;
  const kbs = async (url, options) => { fetched = { url, options }; return { ok: true, json: async () => ({ channel_item: [{ media_type: 'radio', service_url: 'https://1fm.gscdn.kbs.co.kr/stream.m3u8' }] }) }; };
  assert.equal(await resolveRadio({ kbs: '24' }, undefined, kbs), 'https://1fm.gscdn.kbs.co.kr/stream.m3u8');
  assert(fetched.url.endsWith('/24'));
  await assert.rejects(resolveRadio({ kbs: '24' }, undefined, async () => ({ ok: true, json: async () => ({ channel_item: [{ media_type: 'radio', service_url: 'http://127.0.0.1/private' }] }) })), /Invalid/);
  const relay = new RadioRelay(ffmpeg);
  server = http.createServer((req, res) => { const url = new URL(req.url, 'http://local'); void relay.serve(req, res, url.searchParams.get('station'), url.searchParams.get('profile')); });
  await new Promise(r => server.listen(0, '127.0.0.1', r));
  const request = query => fetch(`http://127.0.0.1:${server.address().port}/?${query}`, { signal: AbortSignal.timeout(15000) });
  for (const quality of ['invalid', 'constructor', '__proto__']) assert.equal((await request('station=fixture&profile=' + quality)).status, 400);
  assert.equal((await request('station=http://127.0.0.1&profile=economy')).status, 400);
  const sizes = [];
  for (const [quality, profile] of Object.entries(radioProfiles)) {
    const response = await request(`station=fixture&profile=${quality}`);
    assert.equal(response.status, 200); assert.equal(response.headers.get('icy-br'), String(profile.bitrate));
    assert.equal((await request('station=fixture&profile=economy')).status, 409, 'single encoder admission');
    const bytes = Buffer.from(await response.arrayBuffer()); sizes.push(bytes.length);
    const file = path.join(root, quality + '.mp3'); fs.writeFileSync(file, bytes);
    const media = JSON.parse(execFileSync(ffprobe, ['-v', 'error', '-show_streams', '-of', 'json', file], { windowsHide: true, encoding: 'utf8' })).streams[0];
    assert.equal(media.codec_name, 'mp3'); assert.equal(Number(media.sample_rate), profile.rate); assert.equal(media.channels, profile.channels);
    assert(Math.abs(Number(media.bit_rate) - profile.bitrate * 1000) < 1000);
    await new Promise(r => setTimeout(r, 50)); assert(!relay.active);
  }
  assert(sizes[0] < sizes[1] * .6 && sizes[1] < sizes[2] * .6, 'compression actually reduces transferred bytes');
  const cancelled = await request('station=fixture&profile=economy'); await cancelled.body.cancel();
  for (let i = 0; relay.active && i < 50; ++i) await new Promise(r => setTimeout(r, 20));
  assert(!relay.active, 'disconnect stops encoder and releases admission');
  console.log('PASS: fixed catalog, KBS resolution/host validation, profile validation, MP3 32/64/128k formats and byte reductions, one encoder, disconnect cleanup.');
} finally {
  if (server) { server.closeAllConnections(); await new Promise(r => server.close(r)); }
  if (origin) { origin.closeAllConnections(); await new Promise(r => origin.close(r)); }
  fs.rmSync(root, { recursive: true, force: true });
}

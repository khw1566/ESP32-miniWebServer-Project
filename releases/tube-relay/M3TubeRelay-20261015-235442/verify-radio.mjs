// Read a short public station sample and discard it after MP3 encoding. Never print signed URLs.
import { spawn } from 'node:child_process';
import fs from 'node:fs';
import { radioStations, radioProfiles, radioArgs, resolveRadio } from './radio.mjs';
if (!process.argv.includes('--live')) throw Error('Pass --live to contact public stations');
const configFile = new URL('./config.json', import.meta.url);
const config = fs.existsSync(configFile) ? JSON.parse(fs.readFileSync(configFile, 'utf8')) : {};
const ffmpeg = process.env.M3_TEST_FFMPEG || config.ffmpeg || 'ffmpeg';
if (process.argv.includes('--relay')) {
  if (!config.token) throw Error('Relay token is not configured');
  for (const base of [`http://127.0.0.1:${config.port || 8790}`, 'https://tube.audreaming.com']) {
    const response = await fetch(base + '/api/radio?station=kbs-classic&profile=economy', {
      headers: { 'x-m3-token': config.token }, signal: AbortSignal.timeout(20000),
    });
    if (response.status !== 200 || response.headers.get('icy-br') !== '32') throw Error('Relay profile unavailable');
    const reader = response.body.getReader(); let bytes = 0;
    while (bytes < 4096) { const part = await reader.read(); if (part.done) break; bytes += part.value.length; }
    await reader.cancel();
    if (bytes < 4096) throw Error('Relay returned no MP3 audio');
    console.log(JSON.stringify({ relay: new URL(base).hostname, ok: true, bitrate: 32, bytes }));
    await new Promise(r => setTimeout(r, 1000));
  }
  process.exit(0);
}
async function check(station) {
  const begin = Date.now(); let bytes = 0, firstMs = 0;
  try {
    const source = await resolveRadio(station, AbortSignal.timeout(15000));
    const args = radioArgs(source, radioProfiles.economy); args.splice(-1, 0, '-t', '2');
    await new Promise((resolve, reject) => {
      const child = spawn(ffmpeg, args, { windowsHide: true, stdio: ['ignore', 'pipe', 'pipe'] });
      child.stderr.resume();
      const timer = setTimeout(() => { child.kill(); reject(Error('timeout')); }, 30000);
      child.stdout.on('data', chunk => { firstMs ||= Date.now() - begin; bytes += chunk.length; });
      child.once('error', () => { clearTimeout(timer); reject(Error('encoder unavailable')); });
      child.once('close', code => { clearTimeout(timer); code === 0 && bytes > 4096 ? resolve() : reject(Error('no encoded audio')); });
    });
    return { id: station.id, ok: true, bytes, firstMs, elapsedMs: Date.now() - begin };
  } catch (error) { return { id: station.id, ok: false, code: error.code || error.cause?.code || error.name, bytes, elapsedMs: Date.now() - begin }; }
}
const stations = radioStations.slice(10), results = [];
for (let i = 0; i < stations.length; i += 2) {
  const pair = await Promise.all(stations.slice(i, i + 2).map(check)); results.push(...pair);
  for (const row of pair) console.log(JSON.stringify(row));
}
fs.mkdirSync(new URL('./cache/', import.meta.url), { recursive: true });
fs.writeFileSync(new URL('./cache/radio-verification.json', import.meta.url), JSON.stringify({ checkedAt: new Date().toISOString(), results }, null, 2));
if (results.some(r => !r.ok)) process.exitCode = 1;

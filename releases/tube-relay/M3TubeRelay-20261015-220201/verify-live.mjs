// Explicit live integration check. Reads the existing local token without printing it.
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { PACKET } from './lib.mjs';
if (!process.argv.includes('--live')) { console.error('Use --live for YouTube search/thumbnail requests; add --play for first audio data.'); process.exit(2); }
const root = path.dirname(fileURLToPath(import.meta.url));
try {
  const config = JSON.parse(fs.readFileSync(path.join(root, 'config.json'), 'utf8'));
  if (typeof config.token !== 'string' || config.token.length < 24) throw Error('config');
  const base = `http://127.0.0.1:${Number(config.port) || 8790}`;
  const request = async suffix => {
    const response = await fetch(base + suffix, { headers: { 'x-m3-token': config.token }, signal: AbortSignal.timeout(60000) });
    if (!response.ok) throw Error('request'); return response;
  };
  const result = { evidence: 'PC_relay_live_API_only_not_firmware_or_board', query: 'YOASOBI アイドル' };
  let music;
  for (const kind of ['video', 'music']) {
    const began = performance.now();
    const list = await (await request('/api/search?q=' + encodeURIComponent(result.query) + (kind === 'music' ? '&music=1' : ''))).json();
    if (!list.items?.length) throw Error('empty');
    result[kind] = { count: list.items.length, seconds: Number(((performance.now() - began) / 1000).toFixed(3)),
      japanese_titles: list.items.some(item => /[\u3040-\u30ff\u3400-\u9fff]/u.test(item.title)) };
    if (kind === 'music') music = list.items;
    console.log(`${kind} search passed (${list.items.length} results).`);
  }
  const first = music[0];
  if (!/^[A-Za-z0-9_-]{11}$/.test(first.id)) throw Error('id');
  const thumb = Buffer.from(await (await request(`/api/thumb?id=${first.id}&w=160&h=90`)).arrayBuffer());
  if (thumb.length !== 160 * 90 * 2) throw Error('thumbnail');
  result.thumbnail_bytes = thumb.length;
  if (process.argv.includes('--play')) {
    const response = await request(`/api/play?id=${first.id}&audio=1`), reader = response.body.getReader();
    let pending = Buffer.alloc(0), metadata, audioBytes = 0;
    const began = performance.now();
    try {
      while (!audioBytes) {
        const { value, done } = await reader.read(); if (done) throw Error('eof');
        pending = Buffer.concat([pending, Buffer.from(value)]);
        if (pending.length > 256 * 1024) throw Error('limit');
        while (pending.length >= 12) {
          const length = pending.readUInt32LE(8); if (length > 64 * 1024) throw Error('packet');
          if (pending.length < length + 12) break;
          const type = pending[0], body = pending.subarray(12, length + 12);
          if (type === PACKET.INFO) metadata = JSON.parse(body.toString('utf8'));
          if (type === PACKET.AUDIO) { audioBytes = body.length; break; }
          if (type === PACKET.END) throw Error('play');
          pending = pending.subarray(length + 12);
        }
      }
    } finally { await reader.cancel(); }
    if (!metadata?.audio || metadata.video || metadata.rate !== 32000 || metadata.ch !== 1 || !audioBytes) throw Error('audio');
    result.audio = { rate: metadata.rate, channels: metadata.ch, first_packet_bytes: audioBytes,
      first_audio_seconds: Number(((performance.now() - began) / 1000).toFixed(3)) };
  }
  fs.mkdirSync(path.join(root, 'cache'), { recursive: true });
  fs.writeFileSync(path.join(root, 'cache', 'live-verification.json'), JSON.stringify(result, null, 2) + '\n');
  console.log(JSON.stringify(result, null, 2));
} catch {
  // Never print a fetch/config exception, request headers or token.
  console.error('Live relay verification failed. Check manager status and relay logs; no credentials printed.'); process.exitCode = 1;
}

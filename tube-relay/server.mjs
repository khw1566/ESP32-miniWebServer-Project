// M3 YouTube relay. Runs on a PC; the board reaches it through a Cloudflare Tunnel
// at https://tube.audreaming.com. yt-dlp finds and resolves videos, ffmpeg turns
// them into 480x272 MJPEG + 24 kHz PCM that the ESP32-P4 can decode in hardware.
import http from 'node:http';
import { spawn } from 'node:child_process';
import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { sanitize, searchText, JpegSplitter, PcmChunker, PACKET, packet, Interleaver, validVideoId, validChannelId, toItem, musicItem, videoProfile, videoFilter } from './lib.mjs';
import { MusicCache } from './music_cache.mjs';
import { YouTubeMusic } from './ytmusic.mjs';
import { VideoCache } from './video_cache.mjs';
import { RadioRelay } from './radio.mjs';

const ROOT = path.dirname(fileURLToPath(import.meta.url));
const CONFIG_PATH = process.env.M3_TUBE_CONFIG || path.join(ROOT, 'config.json');
const CACHE_ROOT = process.env.M3_TUBE_CACHE || path.join(ROOT, 'cache');
const config = {
  host: '127.0.0.1', port: 8790, mediaPort: 8791, token: '', ytdlp: ['python', '-m', 'yt_dlp'], ffmpeg: 'ffmpeg',
  pageSize: 10, video: { width: 480, height: 272, fps: 15, quality: 9 }, audio: { rate: 24000, channels: 1, chunkMs: 40 },
  // YouTube Music app: audio only, read from a fully cached local file.
  music: { rate: 32000, channels: 1 },
  ...(fs.existsSync(CONFIG_PATH) ? JSON.parse(fs.readFileSync(CONFIG_PATH, 'utf8')) : {}),
};
if (process.env.M3_TUBE_TOKEN) config.token = process.env.M3_TUBE_TOKEN;
if (!config.token || config.token.length < 24) {
  console.error('config.json has no token. Run: node setup.mjs');
  process.exit(1);
}
const CACHE = path.join(CACHE_ROOT, 'thumb');
fs.mkdirSync(CACHE, { recursive: true });
const log = (...args) => console.log(new Date().toISOString().slice(11, 19), ...args);
const radioRelay = new RadioRelay(config.ffmpeg);

function run(command, args, { timeout = 30000, binary = false, signal } = {}) {
  return new Promise((resolve, reject) => {
    const child = spawn(command, args, { stdio: ['ignore', 'pipe', 'pipe'], windowsHide: true, signal,
      env: { ...process.env, PYTHONUTF8: '1', PYTHONIOENCODING: 'utf-8' } });
    const out = [], err = [];
    let failure = null;
    // Wait for close before callers rename/delete files, especially on Windows.
    const timer = setTimeout(() => { failure = new Error('timeout'); child.kill('SIGKILL'); }, timeout);
    child.stdout.on('data', d => out.push(d));
    child.stderr.on('data', d => err.push(d));
    child.on('error', e => { clearTimeout(timer); failure ||= e; });
    child.on('close', code => {
      clearTimeout(timer);
      if (failure) return reject(failure);
      const data = Buffer.concat(out);
      if (code !== 0) return reject(new Error(Buffer.concat(err).toString().trim().split('\n').pop() || `exit ${code}`));
      resolve(binary ? data : data.toString('utf8'));
    });
  });
}
const ytdlp = (args, options) => run(config.ytdlp[0], [...config.ytdlp.slice(1), '--no-warnings', '--js-runtimes', 'node', ...args], options);
const toolsReady = { ytdlp: false, ffmpeg: false };
run(config.ffmpeg, ['-version']).then(() => { toolsReady.ffmpeg = true; }, error => log(`ffmpeg unavailable: ${error.message}`));

// ---- Search and channel listings (cached for 10 minutes) ----
// Each query keeps the pages it already served. YouTube reorders results between
// requests, so a later page skips videos shown earlier instead of repeating them.
const listCache = new Map();
async function listing(key, source, page, convert = toItem) {
  let entry = listCache.get(key);
  if (!entry || entry.expires < Date.now()) entry = { pages: new Map(), title: '', expires: Date.now() + 600000 };
  listCache.delete(key); listCache.set(key, entry);
  if (listCache.size > 100) listCache.delete(listCache.keys().next().value);
  if (entry.pages.has(page)) return entry.pages.get(page);
  const size = config.pageSize, last = (page + 1) * size; // one spare page absorbs reordering
  const first = Math.max(1, (page - 2) * size + 1);
  const json = JSON.parse(await ytdlp(['--flat-playlist', '-J', '--playlist-items', `${first}:${last}`, source(last)], { timeout: 25000 }));
  const entries = json.entries || [];
  const seen = new Set();
  for (const [number, served] of entry.pages) if (number < page) served.items.forEach(item => seen.add(item.id));
  let pool = entries;
  // Without the earlier pages in memory (expired cache), fall back to plain positions.
  for (let number = 1; number < page; ++number) if (!entry.pages.has(number)) { pool = entries.slice((page - 1) * size - (first - 1)); break; }
  const fresh = pool.map(convert).filter(item => {
    if (!item || seen.has(item.id)) return false;
    seen.add(item.id); return true;
  });
  entry.title ||= sanitize(json.channel || json.uploader || json.title || '', 60);
  const value = { page, more: entries.length >= last - first + 1 - 2, title: entry.title, items: fresh.slice(0, size) };
  if (value.items.length) entry.pages.set(page, value);
  return value;
}
const search = (q, page) => listing(`s:${q}`, last => `ytsearch${last}:${q}`, page);
const channel = (id, page) => listing(`c:${id}`, () => `https://www.youtube.com/channel/${id}/videos`, page);
// Songs come from music.youtube.com; if it fails, the YouTube search without live
// streams, mixes and long videos keeps the app usable.
const youtubeMusic = new YouTubeMusic({ ytdlp, log });
async function musicSearch(q, page) {
  try {
    const value = await youtubeMusic.search(q, page);
    if (value.items.length || page > 1) return value;
  } catch (error) { log(`music.youtube.com search failed: ${error.message}`); }
  return listing(`m:${q}`, last => `ytsearch${last}:${q}`, page, musicItem);
}

// ---- Thumbnails: raw little-endian RGB565, cropped to fill w x h ----
let thumbJobs = 0; const thumbWaiters = [];
async function thumbnail(id, w, h) {
  const file = path.join(CACHE, `${id}_${w}x${h}.rgb565`);
  if (fs.existsSync(file)) return fs.readFileSync(file);
  while (thumbJobs >= 4) await new Promise(r => thumbWaiters.push(r));
  thumbJobs++;
  try {
    for (const name of ['mqdefault', 'hqdefault', 'default']) {
      try {
        const data = await run(config.ffmpeg, ['-hide_banner', '-loglevel', 'error', '-nostdin',
          '-i', `https://i.ytimg.com/vi/${id}/${name}.jpg`,
          '-vf', `scale=${w}:${h}:force_original_aspect_ratio=increase,crop=${w}:${h}`,
          '-frames:v', '1', '-f', 'rawvideo', '-pix_fmt', 'rgb565le', 'pipe:1'], { timeout: 15000, binary: true });
        if (data.length === w * h * 2) { fs.writeFileSync(file, data); return data; }
      } catch { /* try the next size */ }
    }
    return Buffer.alloc(w * h * 2, 0x42); // neutral gray placeholder
  } finally { thumbJobs--; thumbWaiters.shift()?.(); }
}

// ---- Stream resolution (yt-dlp), cached because direct URLs last for hours ----
const FORMAT = 'bv*[height<=360][vcodec^=avc1]+ba[ext=m4a]/bv*[height<=480]+ba/b[height<=480]/b';
const AUDIO_FORMAT = 'ba[ext=m4a]/ba/b';
const infoCache = new Map();
async function resolve(id, audioOnly = false, signal) {
  const key = `${audioOnly ? 'a' : 'v'}:${id}`, hit = infoCache.get(key);
  if (hit && hit.expires > Date.now()) return hit.value;
  const info = JSON.parse(await ytdlp(['-j', '--no-playlist', '-f', audioOnly ? AUDIO_FORMAT : FORMAT, `https://www.youtube.com/watch?v=${id}`], { timeout: 40000, signal }));
  const formats = info.requested_formats || [info];
  const value = {
    title: sanitize(info.title, 150), channel: sanitize(info.channel || info.uploader, 60),
    channel_id: validChannelId(info.channel_id) ? info.channel_id : '',
    duration: Number.isFinite(info.duration) ? Math.round(info.duration) : 0, live: !!info.is_live,
    inputs: formats.map(f => ({
      url: f.url, userAgent: (f.http_headers || {})['User-Agent'] || 'Mozilla/5.0',
      size: f.filesize || Number(new URL(f.url).searchParams.get('clen')) || 0,
      manifest: /m3u8|dash/.test(f.protocol || ''),
      video: f.vcodec && f.vcodec !== 'none', audio: f.acodec && f.acodec !== 'none',
    })),
  };
  infoCache.set(key, { value, expires: Date.now() + 3600000 });
  if (infoCache.size > 100) infoCache.delete(infoCache.keys().next().value);
  return value;
}

const musicCache = new MusicCache(path.join(CACHE_ROOT, 'music'), async (id, file, signal) => {
  // Let yt-dlp handle fragmented/DASH audio and refreshed signed URLs itself.
  // A fixed MP3 format removes source Opus/AAC/container differences on the board.
  const source = file.replace(/\.part$/, '.source');
  try {
    infoCache.delete(`a:${id}`);
    const info = await resolve(id, true, signal);
    if (info.live || !info.duration || info.duration > 1200) throw Error('Choose a non-live song up to 20 minutes');
    for (const [attempt, format] of [AUDIO_FORMAT, 'ba/b'].entries()) {
      try {
        await ytdlp(['--ignore-config', '--no-playlist', '-f', format, '--no-part', '--no-progress',
          '--no-mtime', '--retries', '3', '--fragment-retries', '3', '--socket-timeout', '15',
          '--max-filesize', '96M', '-o', source, `https://www.youtube.com/watch?v=${id}`], { timeout: 90000, signal });
        break;
      } catch (error) {
        fs.rmSync(source, { force: true });
        if (signal.aborted || attempt) throw error;
        log(`music ${id}: retrying with another audio format`);
      }
    }
    if (fs.statSync(source).size > 96 * 1024 * 1024) throw Error('Source audio too large');
    await run(config.ffmpeg, ['-hide_banner', '-loglevel', 'error', '-nostdin', '-n', '-i', source,
      '-vn', '-sn', '-dn', '-t', '1200', '-c:a', 'libmp3lame', '-b:a', '64k', '-ar', '32000', '-ac', '2',
      '-write_xing', '0', '-id3v2_version', '0', '-f', 'mp3', file], { timeout: 90000, signal });
    return { title: info.title, channel: info.channel, channel_id: info.channel_id, duration: info.duration,
      bitrate: 64000, rate: 32000, channels: 2, format: 'mp3' };
  } finally { fs.rmSync(source, { force: true }); }
});

// ---- Loopback media proxy ----
// googlevideo cuts unbounded downloads after ~1.5 MB. ffmpeg sees an ordinary
// seekable HTTP file here, while each upstream request asks for at most 1 MiB.
// This listener is bound to 127.0.0.1 on its own port and is not tunnelled.
const UPSTREAM_CHUNK = 1024 * 1024;
const media = new Map();
function mediaUrl(input) {
  const key = crypto.randomBytes(16).toString('hex');
  media.set(key, { ...input, expires: Date.now() + 6 * 3600000 });
  for (const [k, v] of media) if (v.expires < Date.now()) media.delete(k);
  return `http://127.0.0.1:${config.mediaPort}/${key}`;
}
const mediaServer = http.createServer(async (req, res) => {
  const entry = media.get(req.url.slice(1));
  if (!entry) { res.writeHead(404); return res.end(); }
  const total = entry.size, match = /bytes=(\d*)-(\d*)/.exec(req.headers.range || '');
  let start = match && match[1] ? Number(match[1]) : 0;
  const end = Math.min(total - 1, match && match[2] ? Number(match[2]) : total - 1);
  if (start > end) { res.writeHead(416, { 'Content-Range': `bytes */${total}` }); return res.end(); }
  res.writeHead(match ? 206 : 200, {
    'Content-Type': 'application/octet-stream', 'Accept-Ranges': 'bytes', 'Content-Length': end - start + 1,
    ...(match ? { 'Content-Range': `bytes ${start}-${end}/${total}` } : {}),
  });
  if (req.method === 'HEAD') return res.end();
  const abort = new AbortController();
  res.on('close', () => abort.abort());
  try {
    while (start <= end && !abort.signal.aborted) {
      const last = Math.min(end, start + UPSTREAM_CHUNK - 1);
      let response;
      for (let attempt = 0; attempt < 3; ++attempt) {
        response = await fetch(entry.url, { headers: { 'User-Agent': entry.userAgent, Range: `bytes=${start}-${last}` }, signal: abort.signal });
        if (response.ok) break;
        await new Promise(r => setTimeout(r, 300 * (attempt + 1)));
      }
      if (!response.ok) throw new Error(response.status === 403 && start >= UPSTREAM_CHUNK
        ? 'upstream 403 past the first 1 MiB: update yt-dlp (python -m pip install -U "yt-dlp[default]")' : `upstream ${response.status}`);
      for await (const chunk of response.body) {
        start += chunk.length;
        if (!res.write(chunk)) await new Promise(r => res.once('drain', r));
        if (abort.signal.aborted) break;
      }
    }
    res.end();
  } catch (error) {
    if (!abort.signal.aborted) log(`media proxy: ${error.message}`);
    res.destroy();
  }
});

const videoCache = new VideoCache(path.join(CACHE_ROOT, 'video'), async (id, quality, file, signal) => {
  const info = await resolve(id, false, signal);
  if (info.live || !info.duration || info.duration > 1800) throw new Error('Download requires a non-live video up to 30 minutes');
  if (signal.aborted) throw new Error('cancelled');
  const profile = videoProfile(quality), args = ['-hide_banner', '-loglevel', 'error', '-nostdin', '-n'];
  const keys = [];
  try {
    for (const input of info.inputs) {
      if (input.manifest || !input.size) args.push('-user_agent', input.userAgent, '-i', input.url);
      else { const url = mediaUrl(input); keys.push(new URL(url).pathname.slice(1)); args.push('-i', url); }
    }
    const v = info.inputs.findIndex(i => i.video), a = info.inputs.findIndex(i => i.audio);
    if (v < 0) throw new Error('no video track');
    args.push('-map', `${v}:v:0`);
    if (a >= 0) args.push('-map', `${a}:a:0`);
    args.push('-sn', '-dn', '-t', '1800', '-vf', videoFilter(profile, true),
      '-c:v', 'libx264', '-profile:v', 'baseline', '-level:v', '3.0', '-pix_fmt', 'yuv420p',
      '-bf', '0', '-refs', '1', '-g', String(profile.fps * 2), '-b:v', profile.bitrate,
      '-maxrate', '900k', '-bufsize', '1800k', '-x264-params', 'cabac=0:bframes=0:ref=1',
      '-c:a', 'aac', '-profile:a', 'aac_low', '-ar', '44100', '-ac', '2', '-b:a', '96k',
      '-movflags', '+faststart', '-use_editlist', '0', '-fs', String(512 * 1024 * 1024), '-f', 'mp4', file);
    await run(config.ffmpeg, args, { timeout: 600000, signal });
  } finally { keys.forEach(key => media.delete(key)); }
});

// ---- Playback: one session at a time; a new request replaces the old one ----
let session = null;
async function play(req, res, id, start, audioOnly = false, quality = 'low') {
  if (session) session.stop('replaced');
  // Headers go out at once; WAIT packets keep the connection alive while yt-dlp resolves,
  // so the board can cancel during that wait. Resolve errors arrive as an END packet.
  res.writeHead(200, { 'Content-Type': 'application/octet-stream', 'Cache-Control': 'no-store', 'X-Accel-Buffering': 'no' });
  res.write(packet(PACKET.WAIT, 0));
  const waiting = setInterval(() => res.write(packet(PACKET.WAIT, 0)), 2000);
  let info;
  try {
    if (audioOnly) {
      const song = await musicCache.get(id);
      info = { ...song, live: false, inputs: [{ file: song.file, video: false, audio: true }] };
    } else info = await resolve(id, false);
  }
  catch (error) {
    log(`resolve ${id} failed: ${error.message}`);
    return res.end(packet(PACKET.END, 0, Buffer.from(JSON.stringify({ reason: 'error', error: sanitize(error.message, 120) }))));
  } finally { clearInterval(waiting); }
  if (res.destroyed) return;
  const live = info.live; if (live) start = 0;
  const { width, height } = config.video;
  const profile = videoProfile(quality), { fps } = profile;
  const { rate, channels } = audioOnly ? config.music : config.audio;
  const videoInput = audioOnly ? -1 : info.inputs.findIndex(i => i.video), audioInput = info.inputs.findIndex(i => i.audio);
  if (videoInput < 0 && audioInput < 0) return res.end(packet(PACKET.END, 0, Buffer.from('{"reason":"error","error":"no playable formats"}')));
  const args = ['-hide_banner', '-loglevel', 'error', '-nostdin'];
  for (const input of info.inputs) {
    if (start > 0) args.push('-ss', String(start));
    // Progressive files go through the loopback range proxy; HLS (live) is opened directly.
    if (input.file) args.push('-i', input.file);
    else if (input.manifest || !input.size) args.push('-user_agent', input.userAgent, '-i', input.url);
    else args.push('-i', mediaUrl(input));
  }
  if (videoInput >= 0) args.push('-map', `${videoInput}:v:0`, '-vf',
    videoFilter(profile),
    '-c:v', 'mjpeg', '-q:v', String(profile.quality), '-f', 'mjpeg', 'pipe:1');
  if (audioInput >= 0) args.push('-map', `${audioInput}:a:0`, '-ac', String(channels), '-ar', String(rate), '-f', 's16le', 'pipe:3');
  const child = spawn(config.ffmpeg, args, { stdio: ['ignore', 'pipe', 'pipe', 'pipe'], windowsHide: true });
  const outputs = [child.stdout, child.stdio[3]];

  res.write(packet(PACKET.INFO, 0, Buffer.from(JSON.stringify({
    w: width, h: height, fps, rate, ch: channels, start, duration: info.duration, live,
    video: videoInput >= 0, audio: audioInput >= 0, title: info.title, channel: info.channel, channel_id: info.channel_id,
  }))));
  let stopped = false, frames = 0, bytes = 0, processClosed = false, processCode = null;
  const mux = new Interleaver(buf => { bytes += buf.length; return res.write(buf); }, { video: videoInput >= 0, audio: audioInput >= 0 });
  const pause = () => outputs.forEach(s => s?.pause());
  const resume = () => outputs.forEach(s => s?.resume());
  const pump = () => {
    if (stopped) return;
    if (!mux.pump()) { pause(); res.once('drain', () => { resume(); pump(); }); return; }
    if (mux.finished && processClosed) finish(processCode === 0 && (frames || chunker.total) ? 'eof' : 'ffmpeg error');
  };
  const splitter = new JpegSplitter(frame => { const pts = frames++ * 1000 / fps; mux.addVideo(pts, packet(PACKET.VIDEO, pts, frame)); });
  const frameBytes = 2 * channels;
  const chunker = new PcmChunker(rate * frameBytes * config.audio.chunkMs / 1000, (pcm, offset) => {
    const pts = offset / (rate * frameBytes) * 1000; mux.addAudio(pts, packet(PACKET.AUDIO, pts, pcm));
  }, frameBytes);
  child.stdout.on('data', d => { splitter.push(d); pump(); });
  child.stdout.on('end', () => { mux.endVideo(); pump(); });
  child.stdio[3].on('data', d => { chunker.push(d); pump(); });
  child.stdio[3].on('end', () => { chunker.flush(); mux.endAudio(); pump(); });
  const errors = []; child.stderr.on('data', d => { errors.push(d); log('ffmpeg:', d.toString().trim().slice(0, 300)); });
  const finish = reason => {
    if (stopped) return; stopped = true;
    if (session === current) session = null;
    child.kill('SIGKILL');
    if (!res.writableEnded) res.end(packet(PACKET.END, 0, Buffer.from(JSON.stringify({ reason,
      ...(reason === 'ffmpeg error' ? { error: sanitize(Buffer.concat(errors).toString().trim().slice(-300), 120) || 'Audio conversion failed' } : {}) }))));
    log(`play ${id} t=${start} ended (${reason}) frames=${frames} sent=${(bytes / 1048576).toFixed(1)}MiB`,
      errors.length ? Buffer.concat(errors).toString().trim().slice(-200) : '');
  };
  const current = { stop: finish };
  session = current;
  child.on('error', error => { log(`ffmpeg start failed: ${error.message}`); finish('ffmpeg error'); });
  child.on('close', code => { processClosed = true; processCode = code; mux.endVideo(); mux.endAudio(); pump(); });
  res.on('close', () => finish('client closed'));
  log(`play ${id} t=${start}${audioOnly ? ' audio' : ''} "${info.title}"`);
}

// ---- HTTP ----
function json(res, status, body) {
  const data = Buffer.from(JSON.stringify(body));
  res.writeHead(status, { 'Content-Type': 'application/json; charset=utf-8', 'Content-Length': data.length, 'Cache-Control': 'no-store' });
  res.end(data);
}
function authorized(req) {
  const given = Buffer.from(String(req.headers['x-m3-token'] || ''));
  const expected = Buffer.from(config.token);
  return given.length === expected.length && crypto.timingSafeEqual(given, expected);
}
const server = http.createServer(async (req, res) => {
  const url = new URL(req.url, 'http://relay');
  try {
    if (url.pathname === '/healthz') return json(res, 200, { ok: true, service: 'm3-tube-relay', protocol: 2 });
    if (!authorized(req)) return json(res, 401, { error: 'unauthorized' });
    if (url.pathname === '/api/radio') {
      if (req.method !== 'GET') return json(res, 405, { error: 'GET required' });
      return await radioRelay.serve(req, res, url.searchParams.get('station'), url.searchParams.get('profile') || 'balanced');
    }
    if (url.pathname === '/api/status') return json(res, 200, {
      ok: toolsReady.ytdlp && toolsReady.ffmpeg, service: 'm3-tube-relay', protocol: 2, music_cache: 1, ...toolsReady,
    });
    if (url.pathname === '/api/music') {
      if (req.method !== 'POST') return json(res, 405, { error: 'POST required' });
      const id = url.searchParams.get('id'); if (!validVideoId(id)) return json(res, 400, { error: 'bad song id' });
      const job = musicCache.start(id);
      return json(res, job ? 202 : 409, job ? { job } : { error: 'music preparation busy' });
    }
    if (url.pathname === '/api/music/status' || url.pathname === '/api/music/file') {
      const key = url.searchParams.get('job');
      if (!/^[a-f0-9]{32}$/.test(key || '')) return json(res, 400, { error: 'bad job' });
      const status = musicCache.status(key); if (!status) return json(res, 404, { error: 'job not found' });
      if (url.pathname.endsWith('/status')) {
        if (req.method === 'DELETE') { musicCache.cancel(key); return json(res, 200, { ok: true }); }
        if (req.method !== 'GET') return json(res, 405, { error: 'GET required' });
        return json(res, 200, status);
      }
      if (req.method !== 'GET') return json(res, 405, { error: 'GET required' });
      const file = musicCache.file(key); if (!file) return json(res, 409, { error: 'song not ready' });
      const fd = fs.openSync(file, 'r'), bytes = fs.fstatSync(fd).size;
      const input = fs.createReadStream(file, { fd, autoClose: true });
      res.writeHead(200, { 'Content-Type': 'audio/mpeg', 'Content-Length': bytes, 'Cache-Control': 'no-store' });
      input.on('error', () => res.destroy()); res.on('close', () => input.destroy()); input.pipe(res); return;
    }
    if (url.pathname === '/api/download') {
      if (req.method !== 'POST') return json(res, 405, { error: 'POST required' });
      const id = url.searchParams.get('id'), quality = url.searchParams.get('quality') || 'low';
      if (!validVideoId(id) || !videoProfile(quality)) return json(res, 400, { error: 'bad video or quality' });
      const job = videoCache.start(id, quality);
      return json(res, job ? 202 : 409, job ? { job } : { error: 'download busy' });
    }
    if (url.pathname === '/api/download/status' || url.pathname === '/api/download/file') {
      const key = url.searchParams.get('job');
      if (!/^[a-f0-9]{32}$/.test(key || '')) return json(res, 400, { error: 'bad job' });
      const status = videoCache.status(key);
      if (!status) return json(res, 404, { error: 'job not found' });
      if (url.pathname.endsWith('/status')) {
        if (req.method === 'DELETE') { videoCache.cancel(key); return json(res, 200, { ok: true }); }
        return json(res, 200, status);
      }
      const file = videoCache.file(key);
      if (!file) return json(res, 409, { error: 'download not ready' });
      // An open descriptor survives cache eviction while this response is streaming.
      const fd = fs.openSync(file, 'r'), bytes = fs.fstatSync(fd).size;
      const input = fs.createReadStream(file, { fd, autoClose: true });
      res.writeHead(200, { 'Content-Type': 'video/mp4', 'Content-Length': bytes, 'Cache-Control': 'no-store' });
      input.on('error', () => res.destroy()); res.on('close', () => input.destroy()); input.pipe(res); return;
    }
    const page = Math.min(20, Math.max(1, parseInt(url.searchParams.get('page') || '1', 10) || 1));
    if (url.pathname === '/api/search') {
      const q = searchText(url.searchParams.get('q'), 384);
      if (!q) return json(res, 400, { error: 'empty query' });
      const music = url.searchParams.get('music') === '1';
      log(`${music ? 'music ' : ''}search "${q}" page ${page}`);
      return json(res, 200, await (music ? musicSearch : search)(q, page));
    }
    if (url.pathname === '/api/channel') {
      const id = url.searchParams.get('id');
      if (!validChannelId(id)) return json(res, 400, { error: 'bad channel id' });
      return json(res, 200, await channel(id, page));
    }
    if (url.pathname === '/api/thumb') {
      const id = url.searchParams.get('id'), w = parseInt(url.searchParams.get('w') || '160', 10), h = parseInt(url.searchParams.get('h') || '90', 10);
      if (!validVideoId(id) || !(w >= 16 && w <= 320 && h >= 16 && h <= 180)) return json(res, 400, { error: 'bad thumbnail request' });
      const data = await thumbnail(id, w, h);
      res.writeHead(200, { 'Content-Type': 'application/octet-stream', 'Content-Length': data.length, 'Cache-Control': 'max-age=86400' });
      return res.end(data);
    }
    if (url.pathname === '/api/play') {
      const id = url.searchParams.get('id'), t = Math.max(0, Math.min(86400, parseInt(url.searchParams.get('t') || '0', 10) || 0));
      const quality = url.searchParams.get('quality') || 'low';
      if (!validVideoId(id) || !videoProfile(quality)) return json(res, 400, { error: 'bad video or quality' });
      return await play(req, res, id, t, url.searchParams.get('audio') === '1', quality);
    }
    return json(res, 404, { error: 'not found' });
  } catch (error) {
    log(`error ${url.pathname}: ${error.message}`);
    if (!res.headersSent) json(res, 502, { error: sanitize(error.message, 120) || 'relay error' });
    else res.end();
  }
});
server.requestTimeout = 0;
for (const listener of [server, mediaServer]) listener.on('error', error => {
  console.error(error.code === 'EADDRINUSE' ? 'M3 relay port is already in use. Close the duplicate launcher or check the existing relay.' : error.message);
  process.exit(1);
});
mediaServer.listen(config.mediaPort, '127.0.0.1');
server.listen(config.port, config.host, () => log(`M3 tube relay on http://${config.host}:${config.port}`));
ytdlp(['--version'], { timeout: 20000 }).then(version => { toolsReady.ytdlp = true; log(`yt-dlp ${version.trim()}`); }, error => log(`yt-dlp unavailable: ${error.message}`));

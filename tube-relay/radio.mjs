// Fixed station catalog only: requests cannot supply arbitrary URLs or FFmpeg arguments.
import fs from 'node:fs';
import { spawn } from 'node:child_process';

export const radioStations = JSON.parse(fs.readFileSync(new URL('./radio_stations.json', import.meta.url), 'utf8'));
export const radioProfiles = Object.freeze({
  economy: { bitrate: 32, rate: 22050, channels: 1 },
  balanced: { bitrate: 64, rate: 32000, channels: 1 },
  high: { bitrate: 128, rate: 44100, channels: 2 },
});
export async function resolveRadio(station, signal, fetcher = fetch) {
  if (!station) throw Error('Unknown station');
  if (!station.kbs) return station.url;
  // KBS signs short-lived HLS URLs; resolve afresh, never save a signed URL.
  const response = await fetcher(`https://cfpwwwapi.kbs.co.kr/api/v1/landing/live/channel_code/${station.kbs}`, { signal });
  if (!response.ok) throw Error('KBS channel unavailable');
  const data = await response.json();
  const item = data.channel_item?.find(item => item.media_type === 'radio');
  const url = new URL(item?.service_url || 'invalid:');
  if (url.protocol !== 'https:' || !url.hostname.endsWith('.kbs.co.kr')) throw Error('Invalid KBS stream');
  return url.href;
}
export function radioArgs(source, profile) {
  return ['-hide_banner', '-loglevel', 'error', '-nostdin', '-rw_timeout', '15000000',
    '-reconnect', '1', '-reconnect_streamed', '1', '-reconnect_delay_max', '3',
    '-i', source, '-map', '0:a:0', '-vn', '-sn', '-dn', '-ac', String(profile.channels),
    '-ar', String(profile.rate), '-c:a', 'libmp3lame', '-b:a', `${profile.bitrate}k`,
    '-write_xing', '0', '-id3v2_version', '0', '-flush_packets', '1', '-f', 'mp3', 'pipe:1'];
}
export class RadioRelay {
  constructor(ffmpeg) { this.ffmpeg = ffmpeg; this.active = false; }
  async serve(req, res, id, quality) {
    const station = radioStations.find(s => s.id === id), profile = radioProfiles[quality];
    if (!station || !profile || !Object.hasOwn(radioProfiles, quality)) {
      res.writeHead(400); res.end('Invalid station or profile'); return;
    }
    if (this.active) { res.writeHead(409); res.end('Radio relay busy'); return; }
    this.active = true;
    const abort = new AbortController();
    const timeout = setTimeout(() => abort.abort(), 15000);
    let child, started = false, finished = false;
    const stop = () => {
      if (finished) return;
      finished = true; clearTimeout(timeout); abort.abort();
      if (child) child.kill(); else this.active = false;
    };
    // The board has a short header timeout; provide headers while upstream HLS starts.
    res.writeHead(200, { 'Content-Type': 'audio/mpeg', 'Cache-Control': 'no-store',
      'icy-br': String(profile.bitrate), 'X-M3-Radio-Profile': quality });
    res.flushHeaders();
    res.once('close', stop);
    try {
      const source = await resolveRadio(station, abort.signal);
      if (abort.signal.aborted) return;
      child = spawn(this.ffmpeg, radioArgs(source, profile), { stdio: ['ignore', 'pipe', 'pipe'], windowsHide: true });
      // Do not log upstream errors: HLS requests can contain signed query strings.
      child.stderr.resume();
      child.stdout.once('data', () => {
        if (finished) return;
        started = true; clearTimeout(timeout);
      });
      child.stdout.pipe(res, { end: false }); // Bound memory; close handler reports empty/error streams.
      child.once('error', () => {
        if (!res.headersSent) { res.writeHead(502); res.end('Radio encoder unavailable'); }
        else res.destroy(); stop();
      });
      child.once('close', () => {
        this.active = false;
        if (!started && !res.headersSent && !res.destroyed) { res.writeHead(502); res.end('Radio source unavailable'); }
        else if (!res.writableEnded) res.end();
        stop();
      });
      abort.signal.addEventListener('abort', () => {
        if (!res.headersSent && !res.destroyed) { res.writeHead(504); res.end('Radio source timeout'); }
        else res.destroy(); stop();
      }, { once: true });
    } catch {
      if (!res.headersSent && !res.destroyed) { res.writeHead(502); res.end('Radio source unavailable'); }
      else res.destroy();
      stop();
    }
  }
}

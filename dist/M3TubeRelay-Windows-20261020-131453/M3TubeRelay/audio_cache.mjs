// Audio-only cache for the YouTube Music app. The whole audio track is fetched to
// the PC first (parallel 1 MiB range requests, which googlevideo allows), so the
// board's playback, seeks and replays read a local file instead of a throttled stream.
import fs from 'node:fs';
import path from 'node:path';

const CHUNK = 1024 * 1024, PARALLEL = 4, MAX_FILES = 300;

export class AudioCache {
  constructor(dir, log = () => {}) {
    this.dir = dir; this.log = log; this.pending = new Map();
    fs.mkdirSync(dir, { recursive: true });
  }
  file(id) { return path.join(this.dir, `${id}.audio`); }
  has(id) { return fs.existsSync(this.file(id)); }
  // input: { url, userAgent, size } from the stream resolver. Returns the file path.
  get(id, input) {
    const file = this.file(id);
    if (fs.existsSync(file)) { const now = new Date(); fs.utimesSync(file, now, now); return Promise.resolve(file); }
    if (this.pending.has(id)) return this.pending.get(id);
    const job = this.download(file, input).finally(() => this.pending.delete(id));
    this.pending.set(id, job);
    return job;
  }
  async download(file, { url, userAgent, size }) {
    if (!size) throw new Error('audio size unknown');
    const partial = `${file}.part`, started = Date.now();
    const handle = await fs.promises.open(partial, 'w');
    try {
      let next = 0;
      const worker = async () => {
        while (next < size) {
          const start = next, end = Math.min(size, start + CHUNK) - 1; next = end + 1;
          let data = null;
          for (let attempt = 0; attempt < 3 && !data; ++attempt) {
            const response = await fetch(url, { headers: { 'User-Agent': userAgent, Range: `bytes=${start}-${end}` } });
            if (response.ok) data = Buffer.from(await response.arrayBuffer());
            else await new Promise(r => setTimeout(r, 300 * (attempt + 1)));
          }
          if (!data || data.length !== end - start + 1) throw new Error(`audio range ${start} failed`);
          await handle.write(data, 0, data.length, start);
        }
      };
      await Promise.all(Array.from({ length: PARALLEL }, worker));
    } catch (error) {
      await handle.close(); fs.rmSync(partial, { force: true }); throw error;
    }
    await handle.close();
    fs.renameSync(partial, file);
    this.log(`audio cached ${path.basename(file)} ${(size / 1048576).toFixed(1)}MiB in ${Date.now() - started}ms`);
    this.trim();
    return file;
  }
  // Keeps the newest MAX_FILES tracks (least recently played are removed first).
  trim() {
    const files = fs.readdirSync(this.dir).filter(name => name.endsWith('.audio'))
      .map(name => ({ name, time: fs.statSync(path.join(this.dir, name)).mtimeMs })).sort((a, b) => b.time - a.time);
    for (const { name } of files.slice(MAX_FILES)) fs.rmSync(path.join(this.dir, name), { force: true });
  }
}

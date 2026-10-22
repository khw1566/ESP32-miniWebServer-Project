// Completed, compressed songs shared by board prefetch, SD downloads and legacy playback.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { validVideoId, MUSIC_MAX_SECONDS } from './lib.mjs';

export class MusicCache {
  constructor(dir, prepare, { maxFiles = 64, maxBytes = 256 * 1024 * 1024 } = {}) {
    this.dir = dir; this.prepare = prepare; this.maxFiles = maxFiles; this.maxBytes = maxBytes;
    this.jobs = new Map(); this.pending = new Map();
    fs.mkdirSync(dir, { recursive: true });
    for (const name of fs.readdirSync(dir))
      if (/^[A-Za-z0-9_-]{11}\.mp3\.(part|source)$/.test(name)) fs.rmSync(path.join(dir, name));
  }
  start(id) {
    if (!validVideoId(id)) throw Error('bad song id');
    if (this.pending.has(id)) return this.pending.get(id).key;
    if (this.pending.size >= 2) return null;
    const key = crypto.randomBytes(16).toString('hex'), file = path.join(this.dir, `${id}.mp3`);
    const job = { key, id, file, state: 'preparing', bytes: 0, error: '', abort: new AbortController() };
    this.jobs.set(key, job); this.pending.set(id, job);
    while (this.jobs.size > 64) {
      const old = [...this.jobs].find(([, j]) => j.state !== 'preparing');
      if (!old) break; this.jobs.delete(old[0]);
    }
    job.promise = (async () => {
      const partial = `${file}.part`, meta = `${file}.json`;
      try {
        let info;
        try {
          info = JSON.parse(fs.readFileSync(meta, 'utf8'));
          const bytes = fs.statSync(file).size;
          if (bytes < 128 || bytes > 10 * 1024 * 1024 || !info.duration || info.duration > MUSIC_MAX_SECONDS) throw Error('stale cache');
        } catch {
          info = await this.prepare(id, partial, job.abort.signal);
          if (job.abort.signal.aborted) throw Error('cancelled');
          const bytes = fs.statSync(partial).size;
          if (bytes < 128 || bytes > 10 * 1024 * 1024 || !info.duration || info.duration > MUSIC_MAX_SECONDS) throw Error('invalid song size or duration');
          fs.renameSync(partial, file);
          fs.writeFileSync(meta, JSON.stringify(info));
        }
        if (job.abort.signal.aborted) throw Error('cancelled');
        job.info = info; job.bytes = fs.statSync(file).size;
        const now = new Date(); fs.utimesSync(file, now, now);
        job.state = 'ready'; this.trim(file);
      } catch (error) {
        fs.rmSync(partial, { force: true });
        job.state = job.abort.signal.aborted ? 'cancelled' : 'error'; job.error = error.message;
      } finally { if (this.pending.get(id) === job) this.pending.delete(id); }
    })();
    return key;
  }
  status(key) {
    const job = this.jobs.get(key);
    return job ? { job: key, state: job.state, bytes: job.bytes, error: job.error, ...job.info } : null;
  }
  file(key) { const job = this.jobs.get(key); return job?.state === 'ready' && fs.existsSync(job.file) ? job.file : null; }
  cancel(key) { this.jobs.get(key)?.abort.abort(); }
  async get(id) {
    const key = this.start(id); if (!key) throw Error('Music preparation busy; retry');
    const job = this.jobs.get(key); await job.promise;
    if (job.state !== 'ready') throw Error(job.error);
    return { ...job.info, file: job.file };
  }
  trim(keep) {
    const files = fs.readdirSync(this.dir).filter(n => /^[A-Za-z0-9_-]{11}\.mp3$/.test(n))
      .map(n => { const file = path.join(this.dir, n), stat = fs.statSync(file); return { file, bytes: stat.size, time: stat.mtimeMs }; })
      .sort((a, b) => b.time - a.time);
    let bytes = 0;
    files.forEach((f, i) => {
      bytes += f.bytes;
      if (f.file !== keep && (i >= this.maxFiles || bytes > this.maxBytes)) {
        try { fs.rmSync(f.file); fs.rmSync(`${f.file}.json`, { force: true }); } catch { /* An active Windows reader pins the file. */ }
      }
    });
  }
}

// Bounded offline MP4 jobs. Only completed files can be served to the board.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { validVideoId, videoProfile } from './lib.mjs';

export class VideoCache {
  constructor(dir, prepare, { maxBytes = 512 * 1024 * 1024, maxFiles = 12 } = {}) {
    this.dir = dir; this.prepare = prepare; this.maxBytes = maxBytes; this.maxFiles = maxFiles;
    this.jobs = new Map(); this.active = null;
    fs.mkdirSync(dir, { recursive: true });
    for (const name of fs.readdirSync(dir)) if (/^[A-Za-z0-9_-]{11}_(low|standard|high)\.mp4\.part$/.test(name)) fs.rmSync(path.join(dir, name));
  }
  start(id, quality) {
    if (!validVideoId(id) || !videoProfile(quality)) throw new Error('invalid video or quality');
    if (this.active) return null;
    const key = crypto.randomBytes(16).toString('hex');
    const file = path.join(this.dir, `${id}_${quality}.mp4`);
    const job = { key, file, id, quality, state: 'preparing', bytes: 0, error: '', abort: new AbortController() };
    this.jobs.set(key, job);
    // Bound status history without removing the current job.
    while (this.jobs.size > 32) this.jobs.delete(this.jobs.keys().next().value);
    this.active = job;
    job.promise = (async () => {
      const partial = `${file}.part`;
      try {
        if (!fs.existsSync(file)) {
          await this.prepare(id, quality, partial, job.abort.signal);
          if (job.abort.signal.aborted) throw new Error('cancelled');
          const size = fs.statSync(partial).size;
          if (!size || size > this.maxBytes) throw new Error('video file too large');
          fs.renameSync(partial, file);
        }
        job.bytes = fs.statSync(file).size;
        const now = new Date(); fs.utimesSync(file, now, now);
        job.state = 'ready'; this.trim(file);
      } catch (error) {
        fs.rmSync(partial, { force: true });
        job.state = job.abort.signal.aborted ? 'cancelled' : 'error'; job.error = error.message;
      } finally { if (this.active === job) this.active = null; }
    })();
    return key;
  }
  status(key) {
    const job = this.jobs.get(key);
    return job ? { job: key, state: job.state, bytes: job.bytes, error: job.error } : null;
  }
  file(key) {
    const job = this.jobs.get(key);
    return job?.state === 'ready' && fs.existsSync(job.file) ? job.file : null;
  }
  cancel(key) { const job = this.jobs.get(key); if (job?.state === 'preparing') job.abort.abort(); }
  trim(keep) {
    const files = fs.readdirSync(this.dir).filter(name => /^[A-Za-z0-9_-]{11}_(low|standard|high)\.mp4$/.test(name))
      .map(name => { const file = path.join(this.dir, name), stat = fs.statSync(file); return { file, bytes: stat.size, time: stat.mtimeMs }; })
      .sort((a, b) => b.time - a.time);
    let total = 0;
    files.forEach((entry, index) => {
      total += entry.bytes;
      if (entry.file !== keep && (index >= this.maxFiles || total > this.maxBytes * 2)) fs.rmSync(entry.file, { force: true });
    });
  }
}

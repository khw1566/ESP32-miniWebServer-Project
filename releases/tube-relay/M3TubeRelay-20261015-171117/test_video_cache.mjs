import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { VideoCache } from './video_cache.mjs';
import { VIDEO_PROFILES, videoProfile, videoFilter } from './lib.mjs';

const root = fs.mkdtempSync(path.join(os.tmpdir(), 'm3 video cache '));
try {
  assert.equal(videoProfile('toString'), null); assert.equal(videoProfile('../../high'), null);
  assert.equal(videoProfile(), VIDEO_PROFILES.low);
  assert(VIDEO_PROFILES.low.fps < VIDEO_PROFILES.standard.fps && VIDEO_PROFILES.standard.fps < VIDEO_PROFILES.high.fps);
  for (const p of Object.values(VIDEO_PROFILES)) {
    assert(videoFilter(p).endsWith('format=yuvj420p'));
    assert(videoFilter(p).includes('pad=480:272'));
    assert(videoFilter(p, true).includes('force_divisible_by=16'));
  }
  let release, calls = 0;
  const cache = new VideoCache(root, async (_id, _quality, file) => {
    calls++; await new Promise(r => { release = r; }); fs.writeFileSync(file, Buffer.alloc(32));
  }, { maxBytes: 64, maxFiles: 1 });
  assert.throws(() => cache.start('../bad', 'low'));
  assert.throws(() => cache.start('aaaaaaaaaaa', 'invalid'));
  const first = cache.start('aaaaaaaaaaa', 'low');
  assert.equal(cache.start('bbbbbbbbbbb', 'low'), null, 'only one preparation job');
  assert.equal(cache.file(first), null, 'preparing files cannot be served');
  assert.equal(cache.status(first).state, 'preparing');
  release(); await cache.jobs.get(first).promise;
  assert.equal(cache.status(first).state, 'ready'); assert.equal(cache.status(first).bytes, 32);
  assert(fs.existsSync(cache.file(first))); assert(!fs.existsSync(`${cache.file(first)}.part`));
  const hit = cache.start('aaaaaaaaaaa', 'low'); await cache.jobs.get(hit).promise;
  assert.equal(calls, 1, 'repeat download reuses the completed MP4');
  const cancel = cache.start('bbbbbbbbbbb', 'low'); cache.cancel(cancel); release(); await cache.jobs.get(cancel).promise;
  assert.equal(cache.status(cancel).state, 'cancelled'); assert.equal(cache.file(cancel), null);
  assert(!fs.existsSync(path.join(root, 'bbbbbbbbbbb_low.mp4.part')));
  const second = cache.start('ccccccccccc', 'standard'); release(); await cache.jobs.get(second).promise;
  assert(cache.file(second)); assert.equal(cache.file(first), null, 'cache file count stays bounded');
  const fail = new VideoCache(path.join(root, 'fail'), async (_id, _q, file) => { fs.writeFileSync(file, 'partial'); throw Error('network disconnected'); });
  const failed = fail.start('aaaaaaaaaaa', 'low'); await fail.jobs.get(failed).promise;
  assert.equal(fail.status(failed).state, 'error'); assert.equal(fail.file(failed), null);
  assert.deepEqual(fs.readdirSync(fail.dir), []);
  const large = new VideoCache(path.join(root, 'large'), async (_id, _q, file) => fs.writeFileSync(file, Buffer.alloc(65)), { maxBytes: 64 });
  const oversized = large.start('aaaaaaaaaaa', 'low'); await large.jobs.get(oversized).promise;
  assert.equal(large.status(oversized).state, 'error'); assert.deepEqual(fs.readdirSync(large.dir), []);
  console.log('PASS: quality filters, job admission, completed-only files, cache reuse/eviction, cancellation, preparation failure and size bounds.');
} finally { fs.rmSync(root, { recursive: true, force: true }); }

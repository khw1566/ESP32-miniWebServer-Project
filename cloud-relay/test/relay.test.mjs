import test from 'node:test';
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import worker, { RelayDevice, publicPath } from '../worker.mjs';
const id = 'm1-aabbccddeeff', token = 'a'.repeat(64);
function setup() {
  const env = { PUBLIC_DOMAIN: 'audeaming.com', DEMO_DEVICE: id,
    DEVICE_REGISTRY: JSON.stringify({ [id]: { tokenHash: createHash('sha256').update(token).digest('hex') } }) };
  const device = new RelayDevice({}, env); device.pollMs = 100; device.requestMs = 500;
  env.DEVICES = { idFromName: name => name, get: name => { assert.equal(name, id); return device; } };
  const call = (path, options = {}) => worker.fetch(new Request(`https://relay.audeaming.com/v1/devices/${id}/${path}`, {
    ...options, headers: { authorization: `Bearer ${token}`, ...options.headers }
  }), env);
  const visit = (path = '/', options = {}) => worker.fetch(new Request(`https://demo.audeaming.com${path}`, options), env);
  return { device, env, call, visit };
}
const tick = () => new Promise(resolve => setTimeout(resolve, 0));
async function job(s, path = '/', options) {
  s.device.pollMs = 3000;
  const poll = s.call('poll');
  while (!s.device.poll) await tick();
  const visitor = s.visit(path, options);
  const result = await poll; assert.equal(result.status, 200);
  return { job: await result.json(), visitor };
}
const post = (s, job, action, body, headers = {}) => s.call(`result/${job.id}/${action}`, {
  method: 'POST', body: typeof body === 'object' && !(body instanceof Uint8Array) ? JSON.stringify(body) : body, headers
});
test('only public static website paths are allowed', () => {
  for (const path of ['/', '/index.html', '/assets/site.css', '/movie.mp4']) assert.ok(publicPath(path));
  for (const path of ['/admin', '/api/system', '/setup', '/ai', '/api.json', '/.secret.json', '/../x.html', '/x%2fapi.html', '//x.html']) assert.equal(publicPath(path), false);
});
test('offline, private endpoints, methods and unknown domains', async () => {
  const s = setup();
  assert.equal((await s.visit()).status, 503);
  assert.equal((await s.visit('/admin')).status, 403);
  assert.equal((await s.visit('/', { method: 'POST', body: 'x' })).status, 405);
  assert.equal((await worker.fetch(new Request('https://unknown.audeaming.com/'), s.env)).status, 404);
});
test('device credentials are required; idle poll returns no job', async () => {
  const s = setup();
  assert.equal((await s.call('poll', { headers: { authorization: `Bearer ${'b'.repeat(64)}` } })).status, 401);
  assert.equal((await s.call('poll')).status, 204);
  assert.ok(s.device.lastSeen > 0);
});
test('HTML and arbitrary binary bytes arrive without corruption', async () => {
  const s = setup(); const { job: j, visitor } = await job(s, '/index.html');
  assert.equal(j.path, '/index.html');
  assert.equal((await post(s, j, 'start', { status: 200, headers: { 'content-type': 'application/octet-stream' } })).status, 204);
  const res = await visitor; const received = res.arrayBuffer();
  const bytes = Uint8Array.of(0, 255, 13, 10, 128, 65);
  assert.equal((await post(s, j, 'chunk', bytes, { 'x-sequence': '0' })).status, 204);
  assert.equal((await post(s, j, 'end', { bytes: bytes.length, chunks: 1 })).status, 204);
  assert.deepEqual(new Uint8Array(await received), bytes);
  assert.equal(s.device.jobs.size, 0);
});
test('Range metadata and query stripping', async () => {
  const s = setup(); const { job: j, visitor } = await job(s, '/movie.mp4?v=1', { headers: { range: 'bytes=1-2' } });
  assert.equal(j.path, '/movie.mp4'); assert.equal(j.range, 'bytes=1-2');
  await post(s, j, 'start', { status: 206, headers: { 'content-type': 'video/mp4', 'content-range': 'bytes 1-2/3' } });
  const res = await visitor; const text = res.text(); assert.equal(res.status, 206);
  assert.equal(res.headers.get('content-range'), 'bytes 1-2/3');
  await post(s, j, 'chunk', 'bc', { 'x-sequence': '0' });
  await post(s, j, 'end', { bytes: 2, chunks: 1 }); assert.equal(await text, 'bc');
});
test('open-ended video ranges use bounded windows',async()=>{
 const s=setup();const {job:j,visitor}=await job(s,'/media/video.mp4',{headers:{range:'bytes=0-'}});
 assert.equal(j.range,'bytes=0-262143');
 await post(s,j,'abort',{});assert.equal((await visitor).status,502);
});
test('duplicate chunks cannot be appended; next job recovers', async () => {
  const s = setup(); const { job: j, visitor } = await job(s);
  await post(s, j, 'start', { status: 200 }); const res = await visitor; const text = res.text();
  await post(s, j, 'chunk', 'a', { 'x-sequence': '0' });
  assert.equal((await post(s, j, 'chunk', 'a', { 'x-sequence': '0' })).status, 409);
  await post(s, j, 'end', { bytes: 1, chunks: 1 }); assert.equal(await text, 'a');
  s.device.pollMs = 20; assert.equal((await s.call('poll')).status, 204);
});
test('timeout, stale response and board reconnection', async () => {
  const s = setup(); const { job: j, visitor } = await job(s);
  assert.equal((await visitor).status, 504);
  assert.equal((await post(s, j, 'start', { status: 200 })).status, 410);
  s.device.pollMs = 20; assert.equal((await s.call('poll')).status, 204);
});
test('abort releases pending request and duplicate polls are refused', async () => {
  const s = setup(); const poll = s.call('poll'); while (!s.device.poll) await tick();
  assert.equal((await s.call('poll')).status, 409); await poll;
  const { job: j, visitor } = await job(s);
  assert.equal((await post(s, j, 'abort', {})).status, 204);
  assert.equal((await visitor).status, 502); assert.equal(s.device.jobs.size, 0);
});
test('queue is bounded and unavailable boards cannot accumulate visitors', async () => {
  const s = setup(); s.device.lastSeen = Date.now();
  const waiting = Array.from({ length: 4 }, () => s.visit());
  assert.equal((await s.visit()).status, 503);
  for (const result of await Promise.all(waiting)) assert.equal(result.status, 504);
});
test('oversized device messages and header injection fail closed', async () => {
  const s = setup(); const { job: j, visitor } = await job(s);
  assert.equal((await post(s, j, 'start', { status: 200, headers: { 'content-type': 'text/html\r\nX-Test: bad' } })).status, 400);
  assert.equal((await visitor).status, 502);
  const next = await job(s);
  assert.equal((await post(s, next.job, 'start', 'x'.repeat(1025))).status, 400);
  assert.equal((await next.visitor).status, 502);
});

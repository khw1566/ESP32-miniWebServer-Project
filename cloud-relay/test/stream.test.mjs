import test from 'node:test';
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import worker, { RelayDevice } from '../worker.mjs';
const token = 'c'.repeat(64), id = 'm3-aabbccddeeff';
const tick = () => new Promise(resolve => setTimeout(resolve, 0));
function setup() {
  const env = { PUBLIC_DOMAIN: 'example.test', DEMO_DEVICE: id,
    DEVICE_REGISTRY: JSON.stringify({ [id]: { tokenHash: createHash('sha256').update(token).digest('hex') } }) };
  const device = new RelayDevice({}, env); device.pollMs = 100; device.requestMs = 500;
  device.streamIdleMs = 500; device.streamMaxMs = 3000;
  env.DEVICES = { idFromName: n => n, get: () => device };
  const call = (path, options = {}, version = 1) => worker.fetch(new Request(`https://relay.example.test/v${version}/devices/${id}/${path}`, {
    ...options, ...(options.body instanceof ReadableStream ? { duplex: 'half' } : {}),
    headers: { authorization: `Bearer ${token}`, ...options.headers },
  }), env);
  const visit = (path = '/index.html', options) => worker.fetch(new Request(`https://demo.example.test${path}`, options), env);
  return { device, call, visit };
}
async function job(s, options, capable = true, path = '/index.html') {
  const poll = s.call('poll', { headers: capable ? { 'x-relay-capabilities': 'stream-v2' } : {} });
  while (!s.device.poll) await tick();
  const visitor = s.visit(path, options);
  const j = await (await poll).json();
  assert.equal(j.transport, capable ? 'stream-v2' : undefined);
  return { j, visitor };
}
function send(s, j, length, body, headers = {}) {
  return s.call(`result/${j.id}/stream`, { method: 'POST', body,
    headers: { 'content-length': String(length), 'x-relay-status': '200', 'content-type': 'application/octet-stream', ...headers } }, 2);
}
test('stream-v2 forwards over 8 MiB incrementally with exact binary hash and one data request', async () => {
  const s = setup(), { j, visitor } = await job(s);
  const block = Buffer.from(Array.from({ length: 16384 }, (_, i) => i % 251));
  const size = 10 * 1024 * 1024, expected = createHash('sha256');
  let remaining = size, pulls = 0;
  const input = new ReadableStream({ pull(c) { pulls++; const n = Math.min(remaining, block.length);
    if (!n) { c.close(); return; } const data = block.subarray(0, n); expected.update(data); c.enqueue(data); remaining -= n; } }, { highWaterMark: 0 });
  const posted = send(s, j, size, input);
  const res = await visitor; assert.equal(res.headers.get('x-relay-transport'), 'stream-v2');
  assert.equal(res.headers.get('content-length'), String(size));
  await tick(); assert(pulls <= 1, 'body is not buffered before the visitor reads');
  const actual = createHash('sha256'); let received = 0;
  for await (const bytes of res.body) { actual.update(bytes); received += bytes.length; }
  assert.equal((await posted).status, 204); assert.equal(received, size);
  assert.equal(actual.digest('hex'), expected.digest('hex')); assert.equal(s.device.jobs.size, 0);
});
test('stream Range is preserved without the legacy 256 KiB media window', async () => {
  const s = setup(), { j, visitor } = await job(s, { headers: { range: 'bytes=0-1048575' } }, true, '/media/movie.mp4');
  assert.equal(j.range, 'bytes=0-1048575');
  const posted = send(s, j, 3, Uint8Array.of(0, 128, 255), { 'x-relay-status': '206', 'content-range': 'bytes 0-2/3' });
  const res = await visitor; assert.equal(res.status, 206);
  assert.deepEqual(new Uint8Array(await res.arrayBuffer()), Uint8Array.of(0, 128, 255));
  assert.equal((await posted).status, 204);
});
test('zero-length response, missing files and Range rejection finish', async () => {
  for (const [status, range] of [[200, undefined], [404, undefined], [416, 'bytes */123']]) {
    const s = setup(), { j, visitor } = await job(s);
    const posted = send(s, j, 0, null, { 'x-relay-status': String(status), ...(range ? { 'content-range': range } : {}) });
    const res = await visitor; assert.equal(res.status, status); assert.equal(await res.text(), '');
    assert.equal((await posted).status, 204); assert.equal(s.device.jobs.size, 0);
  }
});
test('zero-length acknowledgement does not depend on a browser consuming the body', async () => {
  const s = setup(), { j, visitor } = await job(s);
  const posted = send(s,j,0,null);
  const res = await visitor;
  assert.equal((await posted).status,204);
  assert.equal(await res.text(),'');
});
test('progress cannot extend the absolute stream lifetime indefinitely', async () => {
  const s = setup(), { j, visitor } = await job(s);
  s.device.streamIdleMs=100;s.device.streamMaxMs=35;
  const body=new ReadableStream({async pull(c){await new Promise(resolve=>setTimeout(resolve,10));try{c.enqueue(Uint8Array.of(1));}catch{}}});
  const posted=send(s,j,100,body),res=await visitor;
  await assert.rejects(res.arrayBuffer());assert.equal((await posted).status,504);
  assert.equal(s.device.jobs.size,0);
});
test('truncated, excess and failing streams never acknowledge success', async () => {
  for (const kind of ['short', 'excess', 'throw']) {
    const s = setup(), { j, visitor } = await job(s);
    const body = kind === 'throw' ? new ReadableStream({ pull() { throw new Error('network failed'); } }) : Uint8Array.of(1, 2);
    const posted = send(s, j, kind === 'short' ? 3 : 1, body);
    const res = await visitor; await assert.rejects(res.arrayBuffer());
    assert.equal((await posted).status, 400); assert.equal(s.device.jobs.size, 0);
  }
});
test('visitor cancellation and idle timeout release reader, job and board request', async () => {
  for (const mode of ['cancel', 'idle']) {
    const s = setup(), { j, visitor } = await job(s); let cancelled = false;
    s.device.streamIdleMs = 30;
    const body = new ReadableStream({ pull() {}, cancel() { cancelled = true; } });
    const posted = send(s, j, 100, body), res = await visitor;
    if (mode === 'cancel') await res.body.cancel();
    else await assert.rejects(res.arrayBuffer());
    assert.equal((await posted).status, mode === 'cancel' ? 410 : 504);
    assert(cancelled); assert.equal(s.device.jobs.size, 0);
  }
});
test('metadata bounds, authentication and v1 capability rejection', async () => {
  for (const headers of [{ 'content-length': String(64 * 1024 * 1024 + 1) }, { 'content-length': 'x' },
      { 'x-relay-status': '302' }, { 'x-relay-status': '206', 'content-range': 'bytes 2-1/3' }]) {
    const s = setup(), { j, visitor } = await job(s);
    assert.equal((await send(s, j, 0, null, headers)).status, 400); assert.equal((await visitor).status, 502);
  }
  const s = setup(), { j, visitor } = await job(s, undefined, false);
  assert.equal((await send(s, j, 0, null)).status, 409);
  assert.equal((await s.call(`result/${j.id}/stream`, { method: 'POST', headers: { authorization: 'Bearer invalid' } }, 2)).status, 401);
  await s.call(`result/${j.id}/abort`, { method: 'POST', body: '{}' }); await visitor;
  assert.equal((await s.call('poll', {}, 2)).status, 404);
});
test('duplicate transfer and board reconnect cannot interleave a running stream', async () => {
  const s = setup(), { j, visitor } = await job(s);
  const posted = send(s, j, 100, new ReadableStream({ pull() {} }));
  const res = await visitor; const content = res.arrayBuffer();
  assert.equal((await send(s, j, 0, null)).status, 409);
  const poll = s.call('poll');
  await assert.rejects(content); assert.equal((await posted).status, 502);
  assert.equal((await poll).status, 204); assert.equal(s.device.jobs.size, 0);
});

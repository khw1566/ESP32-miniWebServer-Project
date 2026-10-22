// Outbound HTTPS long polling keeps the ESP32 implementation small. No local gateway.
export function publicPath(raw) {
  if (typeof raw !== 'string' || raw.length > 180) return false;
  return raw === '/' || (/^\/[A-Za-z0-9_./-]+$/.test(raw) &&
    !raw.includes('..') && !raw.includes('//') && !raw.split('/').some(p => p.startsWith('.')) &&
    !/^\/(api|admin|setup|ai|ai_studio|__relay)(\/|\.|$)/i.test(raw) &&
    /\.(html?|css|js|json|txt|svg|png|jpe?g|gif|webp|ico|mp4|webm|mp3|wav|m4a|pdf|woff2?|ttf)$/i.test(raw));
}
export function mediaName(name) {
  return typeof name === 'string' && name.length <= 64 && /^[A-Za-z0-9_-][A-Za-z0-9_.-]*\.(jpe?g|png|webp|gif|mp4|webm|mp3|wav|m4a)$/i.test(name) && !name.includes('..');
}
async function validToken(request, expected) {
  const auth = request.headers.get('authorization') || '';
  const token = auth.startsWith('Bearer ') ? auth.slice(7) : '';
  if (!/^[a-f0-9]{64}$/.test(token) || !/^[a-f0-9]{64}$/.test(expected || '')) return false;
  const hash = new Uint8Array(await crypto.subtle.digest('SHA-256', new TextEncoder().encode(token)));
  let diff = 0;
  for (let i = 0; i < hash.length; i++) diff |= hash[i] ^ parseInt(expected.slice(i * 2, i * 2 + 2), 16);
  return diff === 0;
}
const response = (status, text = '') => new Response(status === 204 ? null : text, {
  status, headers: { 'content-type': 'text/plain; charset=utf-8', 'cache-control': 'no-store' }
});
async function limitedBody(request, limit) {
  if (!request.body) return new Uint8Array();
  const reader = request.body.getReader(); const chunks = []; let length = 0;
  try {
    for (;;) {
      const { done, value } = await reader.read(); if (done) break;
      length += value.byteLength;
      if (length > limit) { await reader.cancel(); throw new Error('Body too large'); }
      chunks.push(value);
    }
  } finally { reader.releaseLock(); }
  const data = new Uint8Array(length); let pos = 0;
  for (const chunk of chunks) { data.set(chunk, pos); pos += chunk.length; }
  return data;
}
export default {
  async fetch(request, env) {
    const url = new URL(request.url); const domain = env.PUBLIC_DOMAIN;
    let registry;
    try { registry = JSON.parse(env.DEVICE_REGISTRY || '{}'); } catch { return response(503); }
    let id;
    if (url.hostname === `relay.${domain}`) {
      const match = /^\/v[12]\/devices\/([a-z0-9-]{1,32})\/(poll|result\/[a-f0-9-]+\/(start|chunk|end|abort|ack|stream))$/.exec(url.pathname);
      if (!match || url.search) return response(404);
      id = match[1];
      if (!registry[id]) return response(401);
    } else {
      const suffix = `.${domain}`;
      const hostId = url.hostname.endsWith(suffix) ? url.hostname.slice(0, -suffix.length) : '';
      id = (hostId === 'demo' || url.hostname === domain) ? env.DEMO_DEVICE : hostId;
      if (!id || !registry[id]) return response(404, '등록되지 않은 주소입니다.');
    }
    return env.DEVICES.get(env.DEVICES.idFromName(id)).fetch(request);
  }
};
export class RelayDevice {
  constructor(ctx, env) {
    this.env = env; this.queue = []; this.jobs = new Map(); this.poll = null;
    this.active = null; this.lastSeen = 0; this.uploadSession = null;
    this.pollMs = 20000; this.requestMs = 60000;
    this.streamV2 = false; this.streamIdleMs = 15000; this.streamMaxMs = 600000;
  }
  release(job) {
    clearTimeout(job.timer); this.jobs.delete(job.id);
    this.queue = this.queue.filter(j => j !== job);
    if (this.active === job) this.active = null;
  }
  fail(job, status = 504) {
    if (!this.jobs.has(job.id)) return;
    if (job.abortStream) { job.abortStream(status); return; }
    this.release(job);
    if (job.writer) job.writer.abort(new Error('Relay disconnected')).catch(() => {});
    else job.resolve(response(status, '보드의 응답을 받지 못했습니다. 잠시 후 다시 접속하세요.'));
    this.dispatch();
  }
  dispatch() {
    if (!this.poll || this.active) return;
    const job = this.queue.shift(); if (!job) return;
    const poll = this.poll; this.poll = null; clearTimeout(poll.timer);
    this.active = job; job.delivered = true;
    const message = job.kind === 'upload' ? { id: job.id, kind: 'upload', ...job.command } : { id: job.id, path: job.path, range: job.range };
    if (job.kind !== 'upload' && this.streamV2) {
      job.transport = 'stream-v2'; message.transport = 'stream-v2';
    }
    poll.resolve(Response.json(message, { headers: { 'cache-control': 'no-store' } }));
  }
  async fetch(request) {
    try { return await this.handle(request); } catch { return response(400); }
  }
  async handle(request) {
    const url = new URL(request.url);
    if (url.hostname === `relay.${this.env.PUBLIC_DOMAIN}`) {
      const match = /^\/v([12])\/devices\/([a-z0-9-]+)\/(poll|result\/([a-f0-9-]+)\/(start|chunk|end|abort|ack|stream))$/.exec(url.pathname);
      if (!match) return response(404);
      if ((match[1] === '2') !== (match[5] === 'stream')) return response(404);
      const expected = JSON.parse(this.env.DEVICE_REGISTRY || '{}')[match[2]]?.tokenHash;
      if (!await validToken(request, expected)) return response(401);
      this.lastSeen = Date.now();
      if (match[3] === 'poll') {
        if (request.method !== 'GET') return response(405);
        this.streamV2 = request.headers.get('x-relay-capabilities')?.split(',').map(s => s.trim()).includes('stream-v2') === true;
        // A board keeps one poll open, so a new poll means its previous connection
        // is gone (reboot, Wi-Fi drop). Release the stale poll and redeliver a job
        // that never started, instead of refusing the board for up to a minute.
        if (this.poll) { const stale = this.poll; this.poll = null; clearTimeout(stale.timer); stale.resolve(response(204)); }
        if (this.active) {
          const lost = this.active; this.active = null;
          if (lost.writer || lost.busy) this.fail(lost, 502);
          else { lost.delivered = false; this.queue.unshift(lost); }
        }
        return new Promise(resolve => {
          const poll = { resolve, timer: null }; this.poll = poll;
          poll.timer = setTimeout(() => { if (this.poll === poll) this.poll = null; resolve(response(204)); }, this.pollMs);
          this.dispatch();
        });
      }
      if (request.method !== 'POST') return response(405);
      const job = this.jobs.get(match[4]); const action = match[5];
      if (!job || !job.delivered) return response(410);
      if (job.busy) return response(409);
      job.busy = true;
      try {
        if (action === 'stream') return await this.downloadStream(request, job);
        const data = await limitedBody(request, action === 'chunk' ? 2048 : 1024);
        if (!this.jobs.has(job.id)) return response(410);
        if (action === 'ack') {
          if (job.kind !== 'upload') return response(409);
          const result = JSON.parse(new TextDecoder().decode(data));
          if (![200,400,409,422,500,503,507].includes(result.status) || typeof result.message !== 'string' ||
              result.message.length > 160 || !Number.isInteger(result.offset) || result.offset < 0 || result.offset > 8388608) throw new Error('Invalid acknowledgement');
          if(result.status===200 && job.command.action==='chunk' && result.offset!==job.command.offset+atob(job.command.data).length) throw new Error('Incorrect saved offset');
          if(result.status===200 && job.command.action==='begin' && result.offset!==0) throw new Error('Incorrect initial offset');
          if (result.status === 200 && this.uploadSession?.id === job.command.session) {
            if (job.command.action === 'chunk') this.uploadSession.offset = result.offset;
            this.uploadSession.lastActivity = Date.now();
            if (job.command.action === 'commit' || job.command.action === 'abort') this.uploadSession = null;
          } else if (job.command.action === 'begin' || result.status >= 500 || result.status === 422) this.uploadSession = null;
          this.release(job); job.resolve(Response.json(result, { status: result.status, headers: { 'cache-control': 'no-store' } }));
          this.dispatch(); return response(204);
        }
        if (job.kind === 'upload' && action !== 'abort') return response(409);
        if (action === 'abort') { this.fail(job, 502); return response(204); }
        if (action === 'start') {
          if (job.writer) return response(409);
          const meta = JSON.parse(new TextDecoder().decode(data));
          if (![200, 206, 404, 416, 503].includes(meta.status)) throw new Error('Invalid status');
          const headers = { 'cache-control': 'no-store', 'x-content-type-options': 'nosniff', 'referrer-policy': 'no-referrer' };
          for (const key of ['content-type', 'content-range', 'accept-ranges']) {
            const value = meta.headers?.[key];
            if (value !== undefined) {
              if (typeof value !== 'string' || value.length > 160 || /[\r\n\0]/.test(value)) throw new Error('Invalid header');
              headers[key] = value;
            }
          }
          const stream = new TransformStream(); job.writer = stream.writable.getWriter();
          job.writer.closed.catch(() => { if (this.jobs.has(job.id)) this.fail(job, 502); });
          job.resolve(new Response(stream.readable, { status: meta.status, headers }));
        } else if (action === 'chunk') {
          if (!job.writer || request.headers.get('x-sequence') !== String(job.sequence)) return response(409);
          if (!data.length || job.bytes + data.length > 8 * 1024 * 1024) throw new Error('Response too large');
          await job.writer.write(data); job.bytes += data.length; job.sequence++;
        } else if (action === 'end') {
          const meta = JSON.parse(new TextDecoder().decode(data));
          if (!job.writer || meta.bytes !== job.bytes || meta.chunks !== job.sequence) throw new Error('Incomplete response');
          this.release(job); await job.writer.close(); this.dispatch();
        }
        return response(204);
      } catch { this.fail(job, 502); return response(400); }
      finally { job.busy = false; }
    }
    if (url.pathname.startsWith('/__upload/')) {
      if(this.uploadBusy) return response(409, '이전 업로드 요청을 기다리세요.');
      this.uploadBusy=true;
      try { return await this.upload(request,url); } finally { this.uploadBusy=false; }
    }
    if (request.method !== 'GET') return response(405, '공개 웹서비스는 GET 요청만 지원합니다.');
    if (!publicPath(url.pathname)) return response(403, '공개 웹 파일만 접속할 수 있습니다.');
    if (Date.now() - this.lastSeen > 45000) return response(503, '보드가 오프라인입니다. 전원과 Wi-Fi 연결을 확인하세요.');
    if (this.jobs.size >= 4) return response(503, '접속이 많습니다. 잠시 후 다시 시도하세요.');
    let range = request.headers.get('range') || '';
    if (range && (range.length >= 80 || !/^bytes=(\d+-\d*|-\d+)$/.test(range))) return response(416);
    // Browser media reads use bounded windows so one relay request stays short.
    if(!this.streamV2 && /\.(mp4|webm|mp3|wav|m4a)$/i.test(url.pathname) && /^bytes=\d+-\d*$/.test(range)) {
      const [start,end]=range.slice(6).split('-').map(Number);
      if(!Number.isSafeInteger(start) || (range.split('-')[1] && !Number.isSafeInteger(end))) return response(416);
      const cap=start+262143;
      range=`bytes=${start}-${range.split('-')[1]?Math.min(end,cap):cap}`;
    }
    return new Promise(resolve => {
      const job = { id: crypto.randomUUID(), resolve, path: url.pathname, range, bytes: 0, sequence: 0, busy: false, delivered: false };
      this.jobs.set(job.id, job); this.queue.push(job);
      job.timer = setTimeout(() => this.fail(job), this.requestMs);
      this.dispatch();
    });
  }
  async downloadStream(request, job) {
    if (job.kind === 'upload' || job.transport !== 'stream-v2' || job.writer) return response(409);
    const rawLength = request.headers.get('content-length');
    const rawStatus = request.headers.get('x-relay-status');
    if (!/^\d{1,9}$/.test(rawLength || '') || !/^(200|206|404|416|503)$/.test(rawStatus || '')) throw new Error('Invalid stream metadata');
    const length = Number(rawLength), status = Number(rawStatus);
    if (length > 64 * 1024 * 1024) throw new Error('Stream too large');
    const headers = new Headers({ 'cache-control': 'no-store', 'x-content-type-options': 'nosniff',
      'referrer-policy': 'no-referrer', 'content-length': String(length), 'x-relay-transport': 'stream-v2' });
    for (const key of ['content-type', 'content-range', 'accept-ranges']) {
      const value = request.headers.get(key);
      if (value !== null) {
        if (value.length > 160 || /[\r\n\0]/.test(value)) throw new Error('Invalid stream header');
        headers.set(key, value);
      }
    }
    const range = headers.get('content-range');
    if (status === 206) {
      const match = /^bytes (\d+)-(\d+)\/(\d+)$/.exec(range || '');
      if (!job.range || !match || !match.slice(1).every(n => Number.isSafeInteger(Number(n))) ||
          Number(match[2]) < Number(match[1]) || Number(match[2]) >= Number(match[3]) ||
          Number(match[2]) - Number(match[1]) + 1 !== length) throw new Error('Invalid partial stream');
      const total = Number(match[3]), requested = /^bytes=(\d*)-(\d*)$/.exec(job.range);
      if (!requested || !requested.slice(1).filter(Boolean).every(n => Number.isSafeInteger(Number(n)))) throw new Error('Invalid requested range');
      const first = requested[1] ? Number(requested[1]) : Math.max(0, total - Number(requested[2]));
      const last = requested[1] && requested[2] ? Math.min(Number(requested[2]), total - 1) : total - 1;
      if (first !== Number(match[1]) || last !== Number(match[2])) throw new Error('Incorrect range slice');
    } else if (status === 416) {
      if (!/^bytes \*\/\d+$/.test(range || '')) throw new Error('Invalid range rejection');
    } else if (range) throw new Error('Unexpected content range');
    const reader = request.body?.getReader();
    let finished = false, output, acknowledge;
    const completion = new Promise(resolve => { acknowledge = resolve; });
    const started = Date.now();
    const finish = (ok, code = 400) => {
      if (finished) return;
      finished = true; this.release(job); delete job.abortStream;
      if (ok) { reader?.releaseLock(); output.close(); acknowledge(response(204)); }
      else {
        if (reader) reader.cancel('Relay stream ended').then(() => reader.releaseLock()).catch(() => {});
        try { output.error(new Error('Incomplete relay stream')); } catch {}
        acknowledge(response(code));
      }
      this.dispatch();
    };
    const progress = () => {
      clearTimeout(job.timer);
      job.timer = setTimeout(() => finish(false, 504), Math.max(1, Math.min(this.streamIdleMs, this.streamMaxMs - (Date.now() - started))));
    };
    const body = new ReadableStream({
      start: controller => { output = controller; },
      pull: async controller => {
        try {
          const { done, value } = reader ? await reader.read() : { done: true };
          if (finished) return;
          if (done) { finish(job.bytes === length); return; }
          if (!(value instanceof Uint8Array) || job.bytes + value.byteLength > length) { finish(false); return; }
          job.bytes += value.byteLength; this.lastSeen = Date.now(); progress();
          controller.enqueue(value);
        } catch { finish(false); }
      },
      cancel: () => finish(false, 410),
    // At most one incoming chunk is queued; prefetch also detects EOF when a
    // Content-Length client stops reading immediately after the final byte.
    }, { highWaterMark: 1 });
    job.abortStream = code => finish(false, code);
    progress();
    job.resolve(new Response(body, { status, headers }));
    return completion;
  }
  async upload(request, url) {
    if (request.method !== 'POST' || url.search) return response(405);
    const suffix = `.${this.env.PUBLIC_DOMAIN}`;
    const hostId = url.hostname.endsWith(suffix) ? url.hostname.slice(0, -suffix.length) : '';
    const id = hostId === 'demo' || url.hostname === this.env.PUBLIC_DOMAIN ? this.env.DEMO_DEVICE : hostId;
    const record = JSON.parse(this.env.DEVICE_REGISTRY || '{}')[id];
    if (!await validToken(request, record?.uploadTokenHash)) return response(401, '업로드 키를 확인하세요.');
    if (Date.now() - this.lastSeen > 45000) return response(503, '보드가 오프라인입니다.');
    if (this.jobs.size >= 4 || [...this.jobs.values()].some(j => j.kind === 'upload')) return response(409, '이전 업로드 요청을 기다리세요.');
    if (this.uploadSession && Date.now() - this.uploadSession.lastActivity > 300000) this.uploadSession = null;
    const action = url.pathname.slice('/__upload/'.length);
    let command;
    if (action === 'begin') {
      if (this.uploadSession) return response(409, '다른 파일 업로드가 진행 중입니다.');
      const data = JSON.parse(new TextDecoder().decode(await limitedBody(request, 1024)));
      if (!mediaName(data.name) || !Number.isInteger(data.size) || data.size < 1 || data.size > 8388608 || !/^[a-f0-9]{64}$/.test(data.sha256 || '')) return response(400);
      const session = crypto.randomUUID();
      this.uploadSession = { id: session, size: data.size, offset: 0, lastActivity: Date.now() };
      command = { action, session, name: data.name, size: data.size, sha256: data.sha256 };
    } else {
      if (!['chunk','commit','abort'].includes(action)) return response(404);
      const session = request.headers.get('x-upload-session');
      if (!this.uploadSession || this.uploadSession.id !== session) return response(409, '업로드 세션이 만료되었습니다.');
      command = { action, session };
      if (action === 'chunk') {
        const offset = Number(request.headers.get('x-upload-offset'));
        if (!Number.isInteger(offset) || offset !== this.uploadSession.offset) return response(409, '업로드 위치가 다릅니다.');
        const data = await limitedBody(request, 2048);
        if (!data.length || offset + data.length > this.uploadSession.size) return response(400);
        command.offset = offset; command.data = btoa(String.fromCharCode(...data));
      } else if (action === 'commit' && this.uploadSession.offset !== this.uploadSession.size) return response(409, '파일 전송이 완료되지 않았습니다.');
    }
    // Acknowledge to the browser only after the board has written each chunk.
    const result = await new Promise(resolve => {
      const job = { id: crypto.randomUUID(), kind: 'upload', command, resolve, busy: false, delivered: false };
      this.jobs.set(job.id, job); this.queue.push(job);
      job.timer = setTimeout(() => this.fail(job), this.requestMs); this.dispatch();
    });
    const headers = new Headers(result.headers); headers.set('x-upload-session', command.session);
    return new Response(result.body, { status: result.status, headers });
  }
}

// Pure helpers shared by server.mjs and the tests: text cleanup, MJPEG splitting,
// PCM chunking, the M3TV packet format and the packet interleaver.

// The board fonts contain ASCII, Hangul syllables and compatibility jamo only.
const PUNCTUATION = new Map(Object.entries({
  '‘': "'", '’': "'", '“': '"', '”': '"', '–': '-', '—': '-', '―': '-',
  '…': '...', '·': '-', '•': '-', '【': '[', '】': ']', '「': '[', '」': ']',
  '『': '[', '』': ']', '（': '(', '）': ')', '！': '!', '？': '?', '，': ',',
  '：': ':', ' ': ' ', '　': ' ', '｜': '|', '│': '|', '《': '<', '》': '>',
  '〈': '<', '〉': '>', '×': 'x', '♥': '', '♡': '',
}));
export function sanitize(text, maxBytes = 160) {
  let out = '';
  for (const ch of String(text ?? '').normalize('NFC')) {
    const code = ch.codePointAt(0);
    let mapped = '';
    if (code >= 0x20 && code < 0x7f) mapped = ch;
    else if ((code >= 0xac00 && code <= 0xd7a3) || (code >= 0x3131 && code <= 0x318e)) mapped = ch;
    else if (PUNCTUATION.has(ch)) mapped = PUNCTUATION.get(ch);
    else if (code === 0x0a || code === 0x09) mapped = ' ';
    else if (code >= 0xff01 && code <= 0xff5e) mapped = String.fromCharCode(code - 0xfee0); // full-width ASCII
    else if (code >= 0xc0 && code < 0x2000) {
      const base = ch.normalize('NFD').replace(/[̀-ͯ]/g, ''); // e.g. "ễ" -> "e"
      if (/^[\x20-\x7e]+$/.test(base)) mapped = base;
    }
    if (Buffer.byteLength(out + mapped) > maxBytes) break;
    out += mapped;
  }
  return out.replace(/\s+/g, ' ').trim();
}

// ffmpeg's mjpeg muxer emits back-to-back JPEG files. 0xFFD9 cannot occur inside
// entropy-coded data (0xFF is byte-stuffed), so EOI reliably ends each frame.
export class JpegSplitter {
  constructor(onFrame) { this.onFrame = onFrame; this.buffer = Buffer.alloc(0); }
  push(chunk) {
    this.buffer = this.buffer.length ? Buffer.concat([this.buffer, chunk]) : chunk;
    for (;;) {
      const start = this.buffer.indexOf(Buffer.from([0xff, 0xd8]));
      if (start < 0) { // keep a trailing 0xFF: it may be the first half of the next SOI
        this.buffer = this.buffer.length && this.buffer[this.buffer.length - 1] === 0xff ? this.buffer.subarray(-1) : Buffer.alloc(0);
        return;
      }
      const end = this.buffer.indexOf(Buffer.from([0xff, 0xd9]), start + 2);
      if (end < 0) { if (start) this.buffer = this.buffer.subarray(start); return; }
      this.onFrame(Buffer.from(this.buffer.subarray(start, end + 2)));
      this.buffer = this.buffer.subarray(end + 2);
    }
  }
}

export class PcmChunker {
  constructor(bytes, onChunk) { this.bytes = bytes; this.onChunk = onChunk; this.pending = []; this.size = 0; this.total = 0; }
  push(chunk) {
    this.pending.push(chunk); this.size += chunk.length;
    while (this.size >= this.bytes) this.emit(this.bytes);
  }
  flush() { const tail = this.size - (this.size % 4); if (tail) this.emit(tail); }
  emit(length) {
    const all = Buffer.concat(this.pending);
    this.onChunk(Buffer.from(all.subarray(0, length)), this.total);
    this.total += length;
    this.pending = [all.subarray(length)]; this.size = all.length - length;
  }
}

// M3TV packet: u8 type, u8 flags, u16 reserved, u32 pts_ms, u32 length (little endian), payload.
export const PACKET = { INFO: 1, VIDEO: 2, AUDIO: 3, END: 4, WAIT: 5 };
export function packet(type, pts, payload = Buffer.alloc(0)) {
  const header = Buffer.alloc(12);
  header.writeUInt8(type, 0); header.writeUInt32LE(Math.max(0, Math.round(pts)) >>> 0, 4);
  header.writeUInt32LE(payload.length, 8);
  return Buffer.concat([header, payload]);
}
export function parsePackets(buffer) {
  const packets = []; let offset = 0;
  while (offset + 12 <= buffer.length) {
    const length = buffer.readUInt32LE(offset + 8);
    if (offset + 12 + length > buffer.length) break;
    packets.push({ type: buffer[offset], pts: buffer.readUInt32LE(offset + 4), payload: buffer.subarray(offset + 12, offset + 12 + length) });
    offset += 12 + length;
  }
  return { packets, rest: buffer.subarray(offset) };
}

// Emits video and audio packets in presentation order. A stream that has ended
// (or lags far behind) never blocks the other one.
export class Interleaver {
  constructor(write, { video = true, audio = true, limit = 90 } = {}) {
    this.write = write; this.video = []; this.audio = [];
    this.videoDone = !video; this.audioDone = !audio; this.limit = limit;
  }
  addVideo(pts, buf) { this.video.push({ pts, buf }); }
  addAudio(pts, buf) { this.audio.push({ pts, buf }); }
  endVideo() { this.videoDone = true; }
  endAudio() { this.audioDone = true; }
  get finished() { return this.videoDone && this.audioDone && !this.video.length && !this.audio.length; }
  // Returns false when the sink asked to wait (backpressure).
  pump() {
    for (;;) {
      let queue = null;
      const v = this.video, a = this.audio;
      if (v.length && a.length) queue = v[0].pts <= a[0].pts ? v : a;
      else if (v.length && (this.audioDone || v.length > this.limit)) queue = v;
      else if (a.length && (this.videoDone || a.length > this.limit * 3)) queue = a;
      if (!queue) return true;
      const item = queue.shift();
      if (this.write(item.buf) === false) return false;
    }
  }
}

export function validVideoId(id) { return typeof id === 'string' && /^[A-Za-z0-9_-]{11}$/.test(id); }
export function validChannelId(id) { return typeof id === 'string' && /^UC[A-Za-z0-9_-]{22}$/.test(id); }

// yt-dlp flat entry -> compact item for the board.
export function toItem(entry) {
  if (!entry || !validVideoId(entry.id)) return null;
  const url = String(entry.url || '');
  if (entry.ie_key && entry.ie_key !== 'Youtube') return null;
  if (url && !/\/(watch|shorts)/.test(url) && !url.includes(entry.id)) return null;
  const live = entry.live_status === 'is_live' || entry.live_status === 'is_upcoming';
  return {
    id: entry.id,
    title: sanitize(entry.title, 150) || entry.id,
    channel: sanitize(entry.channel || entry.uploader, 60),
    channel_id: validChannelId(entry.channel_id) ? entry.channel_id : '',
    duration: Number.isFinite(entry.duration) ? Math.round(entry.duration) : 0,
    views: Number.isFinite(entry.view_count) ? entry.view_count : 0,
    live,
  };
}

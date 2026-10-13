// Offline tests for the relay helpers. No network, yt-dlp or ffmpeg.
import assert from 'node:assert';
import { sanitize, JpegSplitter, PcmChunker, PACKET, packet, parsePackets, Interleaver, toItem, validVideoId } from './lib.mjs';

// Text: board fonts have ASCII + Hangul only.
assert.equal(sanitize('뉴진스 ‘Ditto’ — Official \u{1F496}MV【HD】'), "뉴진스 'Ditto' - Official MV[HD]");
assert.equal(sanitize('Tyna Nguyễn'), 'Tyna Nguyen');
assert.equal(sanitize('ＡＢＣ　１２３'), 'ABC 123');
assert.equal(sanitize('日本語タイトル abc'), 'abc');
assert(Buffer.byteLength(sanitize('가'.repeat(200), 30)) <= 30, 'UTF-8 byte limit');
assert.equal(sanitize('가'.repeat(200), 31).length, 10, 'never cuts inside a syllable');

// JPEG frames split correctly at any chunk boundary.
const jpeg = n => Buffer.from([0xff, 0xd8, n, 0xff, 0x00, n, 0xff, 0xd9]);
const stream = Buffer.concat([Buffer.from([1, 2]), jpeg(1), jpeg(2), jpeg(3)]);
for (let step = 1; step <= stream.length; ++step) {
  const frames = []; const splitter = new JpegSplitter(f => frames.push(f));
  for (let i = 0; i < stream.length; i += step) splitter.push(stream.subarray(i, i + step));
  assert.deepEqual(frames.map(f => f[2]), [1, 2, 3], `split step ${step}`);
  assert(frames.every(f => f.length === 8));
}

// PCM chunks keep whole stereo frames and exact offsets.
const chunks = []; const chunker = new PcmChunker(3840, (pcm, offset) => chunks.push([pcm.length, offset]));
for (let i = 0; i < 10; ++i) chunker.push(Buffer.alloc(1000));
chunker.flush();
assert.deepEqual(chunks, [[3840, 0], [3840, 3840], [2320, 7680]]);

// Packets round-trip, including a partial tail.
const wire = Buffer.concat([packet(PACKET.INFO, 0, Buffer.from('{}')), packet(PACKET.VIDEO, 66.7, jpeg(9)), packet(PACKET.AUDIO, 40, Buffer.alloc(16))]);
const parsed = parsePackets(Buffer.concat([wire, wire.subarray(0, 5)]));
assert.deepEqual(parsed.packets.map(p => [p.type, p.pts, p.payload.length]), [[1, 0, 2], [2, 67, 8], [3, 40, 16]]);
assert.equal(parsed.rest.length, 5);

// Interleaver: presentation order, no deadlock when one stream ends, backpressure.
const out = []; const mux = new Interleaver(b => { out.push(b); return true; });
mux.addVideo(0, 'v0'); mux.addVideo(66, 'v66'); mux.pump();
assert.deepEqual(out, [], 'waits for audio to decide the order');
mux.addAudio(0, 'a0'); mux.addAudio(40, 'a40'); mux.addAudio(80, 'a80'); mux.pump();
assert.deepEqual(out, ['v0', 'a0', 'a40', 'v66']);
mux.endVideo(); mux.pump();
assert.deepEqual(out.slice(4), ['a80']); mux.endAudio(); assert(mux.finished);
let budget = 2; const slow = []; const limited = new Interleaver(b => { slow.push(b); return --budget > 0; }, { audio: false });
['a', 'b', 'c', 'd'].forEach((x, i) => limited.addVideo(i, x));
assert.equal(limited.pump(), false); assert.deepEqual(slow, ['a', 'b']);
budget = 10; assert.equal(limited.pump(), true); assert.deepEqual(slow, ['a', 'b', 'c', 'd']);
const silent = new Interleaver(b => out.push(b), { limit: 3 });
for (let i = 0; i < 5; ++i) silent.addVideo(i, `x${i}`);
silent.pump(); assert(out.includes('x0'), 'a stalled audio stream cannot hold video forever');

// Search entries: videos only, sanitized, with numeric fields.
assert.deepEqual(toItem({ id: 'ArmDp-zijuc', ie_key: 'Youtube', url: 'https://www.youtube.com/watch?v=ArmDp-zijuc', title: 'Super Shy \u{1F60A}',
  channel: 'HYBE LABELS', channel_id: 'UC3IZKseVpdzPSBaWxBxundA', duration: 200.8, view_count: 5 }),
  { id: 'ArmDp-zijuc', title: 'Super Shy', channel: 'HYBE LABELS', channel_id: 'UC3IZKseVpdzPSBaWxBxundA', duration: 201, views: 5, live: false });
assert.equal(toItem({ id: 'UC3IZKseVpdzPSBaWxBxundA', ie_key: 'YoutubeTab', url: 'https://www.youtube.com/channel/UC3IZKseVpdzPSBaWxBxundA' }), null);
assert.equal(toItem({ id: 'abc' }), null);
assert(!validVideoId('../../etc/pa') && validVideoId('ArmDp-zijuc'));
console.log('PASS: text cleanup, JPEG splitting at every boundary, PCM chunking, packet format, interleave order/backpressure, entry filtering.');

import test from 'node:test';
import assert from 'node:assert/strict';
import { mkdtempSync, readFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { resolve, sep } from 'node:path';
import { fileURLToPath } from 'node:url';
import { spawnSync } from 'node:child_process';
import { createHash } from 'node:crypto';
test('factory credentials match registry and cannot be overwritten', () => {
  const tempRoot = resolve(tmpdir());
  const dir = mkdtempSync(resolve(tempRoot, 'mini-web-provision-'));
  assert.ok(resolve(dir).startsWith(tempRoot + sep));
  try {
    const script = fileURLToPath(new URL('../provision.mjs', import.meta.url));
    const run = serial => spawnSync(process.execPath, [script, serial], { cwd: dir, encoding: 'utf8' });
    assert.notEqual(run('invalid').status, 0);
    const result = run('aabbccddeeff'); assert.equal(result.status, 0);
    const csv = readFileSync(resolve(dir, 'provisioned/m1-aabbccddeeff.csv'), 'utf8');
    const token = /token,data,string,([a-f0-9]{64})/.exec(csv)?.[1]; assert.ok(token);
    assert.equal(result.stdout.includes(token), false);
    const registry = JSON.parse(readFileSync(resolve(dir, 'provisioned/registry.json'), 'utf8'));
    assert.equal(registry['m1-aabbccddeeff'].tokenHash, createHash('sha256').update(token).digest('hex'));
    const uploadToken=readFileSync(resolve(dir,'provisioned/m1-aabbccddeeff-upload-key.txt'),'utf8').trim();
    assert.match(uploadToken,/^[a-f0-9]{64}$/);assert.notEqual(uploadToken,token);
    assert.equal(registry['m1-aabbccddeeff'].uploadTokenHash,createHash('sha256').update(uploadToken).digest('hex'));
    assert.equal(result.stdout.includes(uploadToken),false);
    assert.notEqual(run('aabbccddeeff').status, 0);
    assert.equal(readFileSync(resolve(dir, 'provisioned/m1-aabbccddeeff.csv'), 'utf8'), csv);
  } finally { rmSync(dir, { recursive: true, force: true }); }
});

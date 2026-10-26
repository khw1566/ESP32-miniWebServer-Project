import { randomBytes, createHash } from 'node:crypto';
import { mkdirSync, readFileSync, writeFileSync, existsSync } from 'node:fs';
import { resolve } from 'node:path';
const serial = (process.argv[2] || '').toLowerCase();
if (!/^[a-f0-9]{12}$/.test(serial)) throw new Error('Usage: node provision.mjs <board MAC: 12 hex digits>');
const product = process.argv[3] || 'm1';
if (!['m1', 'm3', 'm4', 's3', 'c3'].includes(product)) throw new Error('Product must be m1, m3, m4, s3 or c3');
const id = `${product}-${serial}`;
const dir = resolve('provisioned'); mkdirSync(dir, { recursive: true });
const registryPath = resolve(dir, 'registry.json');
const registry = existsSync(registryPath) ? JSON.parse(readFileSync(registryPath, 'utf8')) : {};
const csvPath = resolve(dir, `${id}.csv`);
if (registry[id] || existsSync(csvPath)) throw new Error('Already provisioned; refusing to replace credentials');
const token = randomBytes(32).toString('hex');
const uploadToken = randomBytes(32).toString('hex');
registry[id] = { tokenHash: createHash('sha256').update(token).digest('hex'), uploadTokenHash: createHash('sha256').update(uploadToken).digest('hex') };
writeFileSync(resolve(dir, `${id}-upload-key.txt`), uploadToken + '\n', { flag: 'wx', mode: 0o600 });
writeFileSync(csvPath, `key,type,encoding,value\ncloud_relay,namespace,,\ntoken,data,string,${token}\n`, { flag: 'wx', mode: 0o600 });
writeFileSync(registryPath, JSON.stringify(registry, null, 2) + '\n', { mode: 0o600 });
console.log(`Created factory NVS CSV and registry entry for ${id}. Keep provisioned/ private.`);
console.log(`Add to wrangler.toml before deploying:
[[routes]]
pattern = "${id}.audreaming.com"
custom_domain = true`);

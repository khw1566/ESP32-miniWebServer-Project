// Generate server tokens and the private SD installation source. Never embed tokens in firmware.
import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { acceptedTokens } from './relay_auth.mjs';

export function relayUrl(value) {
  const url = new URL(value);
  if (url.protocol !== 'https:' || url.username || url.password || url.search || url.hash || url.pathname !== '/') throw Error('HTTPS relay origin required');
  const origin = url.origin;
  if (origin.length >= 192) throw Error('Relay URL too long');
  return origin;
}
function writeJson(file, value) {
  fs.mkdirSync(path.dirname(file), { recursive: true });
  const temp = file + '.tmp';
  const fd = fs.openSync(temp, 'wx', 0o600);
  try { fs.writeFileSync(fd, JSON.stringify(value, null, 2) + '\n'); fs.fsyncSync(fd); }
  finally { fs.closeSync(fd); }
  if (fs.existsSync(file)) fs.copyFileSync(file, file + '.bak');
  fs.renameSync(temp, file);
}
export function provision(root, { url, count = 3, rotate = false, sdRoot } = {}) {
  if (!Number.isInteger(count) || count < 1 || count > 8) throw Error('Token count must be 1..8');
  const configPath = path.join(root, 'config.json');
  const config = fs.existsSync(configPath) ? JSON.parse(fs.readFileSync(configPath, 'utf8')) : {};
  const origin = relayUrl(url || config.publicUrl || 'https://tube.audreaming.com');
  if (rotate) { delete config.token; config.tokens = []; }
  if (!config.token) config.token = crypto.randomBytes(24).toString('base64url');
  const generated = Array.isArray(config.tokens) ? [...config.tokens] : [];
  while (generated.length < count) generated.push(crypto.randomBytes(24).toString('base64url'));
  config.tokens = generated.slice(0, count);
  config.publicUrl = origin;
  acceptedTokens(config);
  const firmware = path.resolve(root, '../mini-WEB (M3-ESP-IDF_ver)');
  const source = path.join(fs.existsSync(firmware) ? firmware : root, 'sd_private/.private/relays.json');
  const old = fs.existsSync(source) ? JSON.parse(fs.readFileSync(source, 'utf8')) : { version: 1, servers: [] };
  if (old.version !== 1 || !Array.isArray(old.servers)) throw Error('Invalid existing relay list');
  const servers = [...config.tokens.map(token => ({ url: origin, token })), ...old.servers.filter(server => server.url !== origin)];
  if (servers.length > 8) throw Error('SD relay list supports at most 8 profiles');
  for (const server of servers) { relayUrl(server.url); acceptedTokens({ token: server.token }); }
  // Validate the SD destination before changing the server's configuration.
  let destination, merged;
  if (sdRoot) {
    if (!fs.statSync(sdRoot).isDirectory()) throw Error('Mounted SD root required');
    destination = path.join(sdRoot, '.private/relays.json');
    const existing = fs.existsSync(destination) ? JSON.parse(fs.readFileSync(destination, 'utf8')) : { version: 1, servers: [] };
    if (existing.version !== 1 || !Array.isArray(existing.servers)) throw Error('Invalid SD relay list');
    merged = [...servers, ...existing.servers.filter(server => !servers.some(item => item.url === server.url))];
    if (merged.length > 8) throw Error('SD relay list supports at most 8 profiles');
    for (const server of merged) { relayUrl(server.url); acceptedTokens({ token: server.token }); }
  }
  writeJson(configPath, config);
  writeJson(source, { version: 1, servers });
  if (destination) writeJson(destination, { version: 1, servers: merged });
  return { profiles: servers.length, source, url: origin };
}
if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  try {
    const args = process.argv.slice(2), value = name => { const i = args.indexOf(name); return i < 0 ? undefined : args[i + 1]; };
    const result = provision(path.dirname(fileURLToPath(import.meta.url)), {
      url: args.find(arg => arg.startsWith('http')), count: Number(value('--tokens') || 3),
      rotate: args.includes('--rotate'), sdRoot: value('--sd-root'),
    });
    console.log(`Relay SD source saved: ${result.source}\nProfiles: ${result.profiles}. Restart the PC relay; install .private/relays.json on SD and reboot M3. No token values printed.`);
  } catch { console.error('Relay setup failed. Check HTTPS URL, profile limit, existing JSON and SD path. No credentials printed.'); process.exitCode = 1; }
}

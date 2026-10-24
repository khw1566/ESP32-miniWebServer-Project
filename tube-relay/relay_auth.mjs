import crypto from 'node:crypto';

export function validToken(token) {
  return typeof token === 'string' && token.length >= 24 && token.length <= 255 && /^[\x21-\x7e]+$/.test(token);
}
export function acceptedTokens(config) {
  if (config.tokens !== undefined && !Array.isArray(config.tokens)) throw Error('Invalid relay token list');
  const tokens = [config.token, ...(Array.isArray(config.tokens) ? config.tokens : [])].filter(value => value !== undefined && value !== '');
  if (!tokens.length || tokens.length > 9 || tokens.some(token => !validToken(token))) throw Error('Invalid relay token configuration');
  return [...new Set(tokens)];
}
export function tokenAuthorized(given, tokens) {
  const value = Buffer.from(typeof given === 'string' ? given : '');
  let accepted = false;
  for (const token of tokens) {
    const expected = Buffer.from(token);
    if (value.length === expected.length && crypto.timingSafeEqual(value, expected)) accepted = true;
  }
  return accepted;
}

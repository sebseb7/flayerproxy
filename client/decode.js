import { createRequire } from 'node:module';
import chalk from 'chalk';
import { DECODE_MAX } from './constants.js';

const require = createRequire(import.meta.url);

let libchunk;
let libchunkLoadError;
try {
  libchunk = require('../libchunk/js/index.js');
} catch (e) {
  libchunk = null;
  libchunkLoadError = e;
}

export function isLibchunkLoaded() {
  return libchunk !== null;
}

export function warnLibchunkLoadError() {
  if (libchunk) return;
  console.error('[mc-client]', chalk.yellow('libchunk not loaded:'), libchunkLoadError?.message);
  console.error('[mc-client]', chalk.dim('build: cd libchunk && make && cd js && npm run build'));
}

export function decodePayload(packetName, payload) {
  if (!libchunk || !packetName || payload.length === 0) return null;
  if (!libchunk.isPacketSupported(packetName)) return null;
  const r = libchunk.decodePayload(packetName, payload);
  if (!r.ok) {
    if (r.unsupported) return null;
    return chalk.red(r.error || 'decode failed');
  }
  const oneLine = (r.text || '').replace(/\s+/g, ' ').trim();
  if (!oneLine) return null;
  return oneLine.length > DECODE_MAX ? `${oneLine.slice(0, DECODE_MAX)}…` : oneLine;
}

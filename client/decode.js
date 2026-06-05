import { createRequire } from 'node:module';
import chalk from 'chalk';
import { DECODE_MAX, DECODE_MAX_LOGIN } from './constants.js';
import { writeLogLine, isLogSinkOpen } from './logSink.js';
import { readString } from './wire.js';
import { resolveSoundRefs } from './soundNames.js';

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
  const line = (parts) =>
    isLogSinkOpen()
      ? writeLogLine(parts.filter(Boolean).join(' '))
      : console.error(...parts);
  line([chalk.bgBlue.black.bold(' mc-client '), chalk.yellow('libchunk not loaded:'), libchunkLoadError?.message]);
  line([
    chalk.bgBlue.black.bold(' mc-client '),
    chalk.dim('build: cd libchunk && make && cd js && npm run build'),
  ]);
}

export function decodePayload(packetName, payload) {
  if (packetName === 'status_request') {
    return 'status_request{}';
  }
  if (packetName === 'status_response') {
    const s = readString(payload, 0);
    return s ? `status_response{json=${s.value}}` : null;
  }
  if (packetName === 'ping' || packetName === 'pong') {
    if (payload.length === 8) {
      try {
        const val = payload.readBigInt64BE(0);
        return `${packetName}{id=${val}}`;
      } catch (e) {}
    } else if (payload.length === 4) {
      try {
        const val = payload.readInt32BE(0);
        return `${packetName}{id=${val}}`;
      } catch (e) {}
    }
  }

  if (!libchunk || !packetName || payload.length === 0) return null;
  if (!libchunk.isPacketSupported(packetName)) return null;
  const r = libchunk.decodePayload(packetName, payload);
  if (!r.ok) {
    if (r.unsupported) return null;
    return chalk.red(r.error || 'decode failed');
  }
  let oneLine = (r.text || '').replace(/\s+/g, ' ').trim();
  if (!oneLine) return null;
  if (packetName === 'sound_effect' || packetName === 'entity_sound_effect') {
    oneLine = resolveSoundRefs(oneLine);
  }
  if (packetName === 'entity_update_attributes') {
    return oneLine;
  }
  const maxLen = packetName === 'login' ? DECODE_MAX_LOGIN : DECODE_MAX;
  return oneLine.length > maxLen ? `${oneLine.slice(0, maxLen)}…` : oneLine;
}

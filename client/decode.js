'use strict';

const chalk = require('chalk');
const { DECODE_MAX } = require('./constants');

let libchunk;
let libchunkLoadError;
try {
  libchunk = require('../libchunk/js');
} catch (e) {
  libchunk = null;
  libchunkLoadError = e;
}

function isLibchunkLoaded() {
  return libchunk !== null;
}

function warnLibchunkLoadError() {
  if (libchunk) return;
  console.error('[mc-client]', chalk.yellow('libchunk not loaded:'), libchunkLoadError?.message);
  console.error('[mc-client]', chalk.dim('build: cd libchunk && make && cd js && npm run build'));
}

function decodePayload(packetName, payload) {
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

module.exports = {
  isLibchunkLoaded,
  warnLibchunkLoadError,
  decodePayload,
};

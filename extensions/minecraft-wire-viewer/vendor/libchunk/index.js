'use strict';

const fs = require('fs');
const path = require('path');
const wirePath = require('./wirePath');

function loadNative() {
  const candidates = [
    './build/Release/libchunk.node',
    './build/Debug/libchunk.node',
    './build/Release/chunk.node',
    './build/Debug/chunk.node',
  ];
  for (const p of candidates) {
    try {
      return require(p);
    } catch {
      /* try next */
    }
  }
  throw new Error(
    'libchunk native addon not built. Run: cd libchunk && make && cd js && npm install'
  );
}

const native = loadNative();

/**
 * Decode wire bytes to libchunk toString summary.
 * @param {string} packetName
 * @param {Buffer} buffer
 * @returns {{ ok: boolean, text?: string, error?: string, unsupported?: boolean }}
 */
function decodeWire(packetName, buffer) {
  if (!Buffer.isBuffer(buffer)) buffer = Buffer.from(buffer);
  return native.decodeWire(packetName, buffer);
}

/**
 * Full map_chunk JSON decode.
 * @param {string} basename - file basename for JSON metadata
 * @param {Buffer} buffer
 */
function decodeMapChunkJson(basename, buffer) {
  if (!Buffer.isBuffer(buffer)) buffer = Buffer.from(buffer);
  const result = native.decodeMapChunkJson(basename, buffer);
  if (result.ok && result.json) {
    try {
      result.parsed = JSON.parse(result.json);
    } catch {
      /* keep raw json string */
    }
  }
  return result;
}

/**
 * Decode using packet inferred from sniffer path.
 * @param {string} filePath
 * @param {Buffer} [buffer] - if omitted, read from disk
 */
function decodeWireFile(filePath, buffer) {
  const packet = wirePath.packetNameFromPath(filePath);
  if (!packet) {
    return { ok: false, error: `cannot infer packet from path: ${filePath}` };
  }
  if (!native.isPacketSupported(packet)) {
    return { ok: false, unsupported: true, error: `packet not decoded by libchunk: ${packet}` };
  }
  const wire = buffer ?? fs.readFileSync(filePath);
  const meta = wirePath.parseWirePath(filePath);
  const result = decodeWire(packet, wire);
  return { ...result, packet, meta };
}

function hexDump(buffer) {
  if (!Buffer.isBuffer(buffer)) buffer = Buffer.from(buffer);
  return native.hexDump(buffer);
}

module.exports = {
  ...wirePath,
  supportedPackets: () => native.supportedPackets(),
  isPacketSupported: (name) => native.isPacketSupported(name),
  decodeWire,
  decodeMapChunkJson,
  decodeWireFile,
  hexDump,
};

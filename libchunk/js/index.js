'use strict';

const fs = require('fs');
const path = require('path');
const wirePath = require('./wirePath');
const playS2cById = require('./playS2cById');

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
 * Decode packet payload (no leading packet-id varint) to libchunk toString summary.
 * @param {string} packetName
 * @param {Buffer} buffer
 * @returns {{ ok: boolean, text?: string, error?: string, unsupported?: boolean }}
 */
function decodePayload(packetName, buffer) {
  if (!Buffer.isBuffer(buffer)) buffer = Buffer.from(buffer);
  return native.decodePayload(packetName, buffer);
}

/**
 * Decode sniffer .wire bytes (packet-id varint + payload).
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

function tryReadFrame(buf) {
  if (!Buffer.isBuffer(buf)) buf = Buffer.from(buf);
  const r = native.tryReadFrame(buf);
  if (!r.complete) return null;
  return {
    id: r.id,
    payload: r.payload,
    rest: buf.subarray(r.consumed),
  };
}

/**
 * @param {(id: number, payload: Buffer) => void} onFrame
 * @returns {(chunk: Buffer) => void} feed — pass to sock.on('data', feed)
 */
function createFrameProcessor(onFrame) {
  const proc = new native.FrameProcessor(onFrame);
  function feed(chunk) {
    if (!Buffer.isBuffer(chunk)) chunk = Buffer.from(chunk);
    return proc.feed(chunk);
  }
  feed.reset = () => proc.reset();
  return feed;
}

module.exports = {
  ...wirePath,
  playS2cById,
  FrameProcessor: native.FrameProcessor,
  supportedPackets: () => native.supportedPackets(),
  isPacketSupported: (name) => native.isPacketSupported(name),
  decodePayload,
  decodeWire,
  decodeMapChunkJson,
  decodeWireFile,
  hexDump,
  tryReadFrame,
  createFrameProcessor,
  buildFrame: (id, payload) => {
    if (payload === undefined) payload = Buffer.alloc(0);
    if (!Buffer.isBuffer(payload)) payload = Buffer.from(payload);
    return native.buildFrame(id, payload);
  },
  writeVarInt: (n) => native.writeVarInt(n),
  writeString: (s) => native.writeString(s),
  readVarIntAt: (buf, off) => native.readVarIntAt(buf, off),
  readStringAt: (buf, off) => native.readStringAt(buf, off),
  peekMapChunkCoords: (buffer) => {
    if (!Buffer.isBuffer(buffer)) buffer = Buffer.from(buffer);
    return native.peekMapChunkCoords(buffer);
  },
  parsePlayLogin: (buffer) => {
    if (!Buffer.isBuffer(buffer)) buffer = Buffer.from(buffer);
    return native.parsePlayLogin(buffer);
  },
  parsePosition: (buffer) => {
    if (!Buffer.isBuffer(buffer)) buffer = Buffer.from(buffer);
    return native.parsePosition(buffer);
  },
  parseUpdateTime: (buffer) => {
    if (!Buffer.isBuffer(buffer)) buffer = Buffer.from(buffer);
    return native.parseUpdateTime(buffer);
  },
  parseGameEvent: (buffer) => {
    if (!Buffer.isBuffer(buffer)) buffer = Buffer.from(buffer);
    return native.parseGameEvent(buffer);
  },
  parseSetTickingState: (buffer) => {
    if (!Buffer.isBuffer(buffer)) buffer = Buffer.from(buffer);
    return native.parseSetTickingState(buffer);
  },
  parseUpdateHealth: (buffer) => {
    if (!Buffer.isBuffer(buffer)) buffer = Buffer.from(buffer);
    return native.parseUpdateHealth(buffer);
  },
  parseUpdateViewPosition: (buffer) => {
    if (!Buffer.isBuffer(buffer)) buffer = Buffer.from(buffer);
    return native.parseUpdateViewPosition(buffer);
  },
  parseEntityVelocity: (buffer) => {
    if (!Buffer.isBuffer(buffer)) buffer = Buffer.from(buffer);
    return native.parseEntityVelocity(buffer);
  },
  parseRelEntityMove: (buffer) => {
    if (!Buffer.isBuffer(buffer)) buffer = Buffer.from(buffer);
    return native.parseRelEntityMove(buffer);
  },
  parseSyncEntityPosition: (buffer) => {
    if (!Buffer.isBuffer(buffer)) buffer = Buffer.from(buffer);
    return native.parseSyncEntityPosition(buffer);
  },
  parseEntityHeadRotation: (buffer) => {
    if (!Buffer.isBuffer(buffer)) buffer = Buffer.from(buffer);
    return native.parseEntityHeadRotation(buffer);
  },
  parseEntityMoveLook: (buffer) => {
    if (!Buffer.isBuffer(buffer)) buffer = Buffer.from(buffer);
    return native.parseEntityMoveLook(buffer);
  },
  parseEntityLook: (buffer) => {
    if (!Buffer.isBuffer(buffer)) buffer = Buffer.from(buffer);
    return native.parseEntityLook(buffer);
  },
  parseEntityMetadata: (buffer) => {
    if (!Buffer.isBuffer(buffer)) buffer = Buffer.from(buffer);
    return native.parseEntityMetadata(buffer);
  },
  parseEntityEquipment: (buffer) => {
    if (!Buffer.isBuffer(buffer)) buffer = Buffer.from(buffer);
    return native.parseEntityEquipment(buffer);
  },
  parseSpawnEntity: (buffer) => {
    if (!Buffer.isBuffer(buffer)) buffer = Buffer.from(buffer);
    return native.parseSpawnEntity(buffer);
  },
  parseEntityTeleport: (buffer) => {
    if (!Buffer.isBuffer(buffer)) buffer = Buffer.from(buffer);
    return native.parseEntityTeleport(buffer);
  },
  parseEntityDestroy: (buffer) => {
    if (!Buffer.isBuffer(buffer)) buffer = Buffer.from(buffer);
    return native.parseEntityDestroy(buffer);
  },
  parseSetPassengers: (buffer) => {
    if (!Buffer.isBuffer(buffer)) buffer = Buffer.from(buffer);
    return native.parseSetPassengers(buffer);
  },
  parseEntityUpdateAttributes: (buffer) => {
    if (!Buffer.isBuffer(buffer)) buffer = Buffer.from(buffer);
    return native.parseEntityUpdateAttributes(buffer);
  },
  parseEntityStatus: (buffer) => {
    if (!Buffer.isBuffer(buffer)) buffer = Buffer.from(buffer);
    return native.parseEntityStatus(buffer);
  },
  parseEntityEffect: (buffer) => {
    if (!Buffer.isBuffer(buffer)) buffer = Buffer.from(buffer);
    return native.parseEntityEffect(buffer);
  },
  parseRemoveEntityEffect: (buffer) => {
    if (!Buffer.isBuffer(buffer)) buffer = Buffer.from(buffer);
    return native.parseRemoveEntityEffect(buffer);
  },
  parseAttachEntity: (buffer) => {
    if (!Buffer.isBuffer(buffer)) buffer = Buffer.from(buffer);
    return native.parseAttachEntity(buffer);
  },
};


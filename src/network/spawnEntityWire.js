'use strict';

const LpVec3 = require('./LpVec3');

const { read: readLpVec3 } = LpVec3;

function readVarInt(buffer, offset) {
  let out = 0;
  let bytes = 0;
  let pos = offset;
  while (true) {
    const in_ = buffer[pos];
    out |= (in_ & 127) << (bytes++ * 7);
    if (bytes > 5) throw new Error('VarInt too big');
    pos += 1;
    if ((in_ & 128) !== 128) break;
  }
  return { value: out, size: pos - offset };
}

/**
 * Parse spawn_entity play packet buffer (includes leading packet id varint).
 * Field order: packetId, entityId, UUID, type, x/y/z f64, lpVec3 velocity, …
 *
 * @param {Buffer} buffer
 * @returns {{ velocity: { x: number, y: number, z: number }, velocityHex: string, lpSize: number, velocityOffset: number }}
 */
function parseSpawnEntityVelocity(buffer) {
  if (!buffer?.length) {
    throw new Error('empty spawn_entity buffer');
  }

  let off = 0;
  const packetId = readVarInt(buffer, off);
  off += packetId.size;

  const entityId = readVarInt(buffer, off);
  off += entityId.size;

  off += 16; // UUID

  const entityType = readVarInt(buffer, off);
  off += entityType.size;

  off += 24; // x, y, z as f64

  if (off >= buffer.length) {
    throw new Error(`spawn_entity buffer too short for velocity (offset ${off}, len ${buffer.length})`);
  }

  const { value: velocity, size: lpSize } = readLpVec3(buffer, off);

  return {
    packetId: packetId.value,
    velocity,
    velocityHex: buffer.subarray(off, off + lpSize).toString('hex'),
    lpSize,
    velocityOffset: off,
  };
}

module.exports = { parseSpawnEntityVelocity };

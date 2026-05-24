'use strict';

/**
 * Port of net.minecraft.network.LpVec3 (serversSrc / MC 26.2.1).
 * Uses BigInt for 48-bit buffer assembly — JS Number loses bits above ~2^48.
 */

const DATA_BITS_MASK = 32767n;
const MAX_QUANTIZED_VALUE = 32766.0;
const CONTINUATION_FLAG = 4;

function hasContinuationBit(in_) {
  return (in_ & CONTINUATION_FLAG) === CONTINUATION_FLAG;
}

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

function unpack(value) {
  const masked = value & DATA_BITS_MASK;
  const clamped = Number(masked > 32766n ? 32766n : masked);
  return (clamped * 2.0) / MAX_QUANTIZED_VALUE - 1.0;
}

/**
 * @param {Buffer} buffer
 * @param {number} [offset]
 * @returns {{ value: { x: number, y: number, z: number }, size: number }}
 */
function read(buffer, offset = 0) {
  const lowest = buffer.readUInt8(offset);
  if (lowest === 0) {
    return { value: { x: 0, y: 0, z: 0 }, size: 1 };
  }

  const middle = buffer.readUInt8(offset + 1);
  const highest = buffer.readUInt32LE(offset + 2);
  let packed =
    (BigInt(highest) << 16n) | (BigInt(middle) << 8n) | BigInt(lowest);

  let scale = BigInt(lowest & 3);
  let size = 6;

  if (hasContinuationBit(lowest)) {
    const { value: varIntVal, size: varIntSize } = readVarInt(buffer, offset + 6);
    scale |= BigInt(varIntVal) << 2n;
    size += varIntSize;
  }

  const s = Number(scale);
  return {
    value: {
      x: unpack(packed >> 3n) * s,
      y: unpack(packed >> 18n) * s,
      z: unpack(packed >> 33n) * s,
    },
    size,
  };
}

/** Extract lpVec3 bytes after packet id + varint entityId. */
function readAfterEntityId(wireHex) {
  const buf = Buffer.from(wireHex, 'hex');
  let off = 0;
  const id = buf[off];
  off += 1;
  const { size: entityIdSize } = readVarInt(buf, off);
  off += entityIdSize;
  return read(buf, off);
}

/** lpVec3 at end of spawn_entity payload (after 3×f64 position). */
const SPAWN_VELOCITY_HEX = 'f9ff7fff04d9';

module.exports = {
  read,
  readAfterEntityId,
  SPAWN_VELOCITY_HEX,
};

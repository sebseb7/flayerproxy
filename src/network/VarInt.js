'use strict';

/** Ported from net.minecraft.network.VarInt (serversSrc 26.2.1). */

function hasContinuationBit(byte) {
  return (byte & 128) === 128;
}

/**
 * @param {Buffer} input
 * @param {number} [offset]
 * @returns {{ value: number, size: number }}
 */
function readVarInt(input, offset = 0) {
  let out = 0;
  let bytes = 0;
  let pos = offset;

  let byte;
  do {
    byte = input.readUInt8(pos++);
    out |= (byte & 127) << (bytes++ * 7);
    if (bytes > 5) {
      throw new Error('VarInt too big');
    }
  } while (hasContinuationBit(byte));

  return { value: out >>> 0, size: pos - offset };
}

/**
 * @param {Buffer} input
 * @param {number} [offset]
 */
function varIntSize(input, offset = 0) {
  return readVarInt(input, offset).size;
}

module.exports = { readVarInt, varIntSize };

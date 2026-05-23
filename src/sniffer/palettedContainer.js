'use strict';

const BLOCK_VOLUME = 16 * 16 * 16;

/** Bits per palette entry; vanilla bumps 1–3 up to 4 for block states. */
function bitsForBlockPalette(paletteLength) {
  if (paletteLength <= 1) return 0;
  let bits = Math.ceil(Math.log2(paletteLength));
  if (bits >= 1 && bits <= 3) bits = 4;
  return bits;
}

function bitsForBiomePalette(paletteLength) {
  if (paletteLength <= 1) return 0;
  return Math.ceil(Math.log2(paletteLength));
}

/** Vanilla Anvil PalettedContainer long count (not ceil(capacity * bits / 64)). */
function palettedStorageLongCount(capacity, bitsPerValue) {
  return Math.ceil(capacity / Math.floor(64 / bitsPerValue));
}

function blockIndex(localX, localY, localZ) {
  return (localY << 8) | (localZ << 4) | localX;
}

function getPackedIndex(data, bitsPerValue, index) {
  const bitIndex = index * bitsPerValue;
  const startLongIndex = bitIndex >>> 5;
  const indexInStartLong = bitIndex & 31;
  let result = data[startLongIndex] >>> indexInStartLong;
  const endBitOffset = indexInStartLong + bitsPerValue;
  if (endBitOffset > 32) {
    const bitsInNextLong = endBitOffset - 32;
    result |= (data[startLongIndex + 1] << (bitsPerValue - bitsInNextLong)) >>> 0;
  }
  return result & ((1 << bitsPerValue) - 1);
}

function setPackedIndex(data, bitsPerValue, index, value) {
  const mask = (1 << bitsPerValue) - 1;
  const v = value & mask;
  const bitIndex = index * bitsPerValue;
  const startLongIndex = bitIndex >>> 5;
  const indexInStartLong = bitIndex & 31;
  data[startLongIndex] =
    (data[startLongIndex] & ~(mask << indexInStartLong)) | (v << indexInStartLong);
  const endBitOffset = indexInStartLong + bitsPerValue;
  if (endBitOffset > 32) {
    const bitsInNextLong = endBitOffset - 32;
    data[startLongIndex + 1] =
      (data[startLongIndex + 1] & ~((1 << bitsInNextLong) - 1)) |
      (v >>> (bitsPerValue - bitsInNextLong));
  }
}

/** NBT longArray → Uint32Array (Anvil layout). */
function longArrayToUint32(dataTag) {
  const raw = dataTag?.value ?? dataTag;
  if (!raw) return new Uint32Array(0);
  if (Buffer.isBuffer(raw)) {
    return new Uint32Array(raw.buffer, raw.byteOffset, raw.byteLength / 4);
  }
  if (typeof raw[0] === 'number' && !Array.isArray(raw[0])) {
    return Uint32Array.from(raw);
  }
  const out = [];
  for (const entry of raw) {
    if (Array.isArray(entry)) {
      out.push(entry[0] >>> 0, entry[1] >>> 0);
    } else if (typeof entry === 'bigint') {
      out.push(Number(entry & 0xffffffffn), Number((entry >> 32n) & 0xffffffffn));
    }
  }
  return Uint32Array.from(out);
}

/** Pack palette indices into Anvil long array (vanilla bit order). */
function packPaletteIndices(paletteIndices, bitsPerValue) {
  const longCount = palettedStorageLongCount(BLOCK_VOLUME, bitsPerValue);
  const data = new Uint32Array(longCount * 2);
  for (let i = 0; i < BLOCK_VOLUME; i++) {
    setPackedIndex(data, bitsPerValue, i, paletteIndices[i]);
  }
  const longs = [];
  for (let i = 0; i < longCount; i++) {
    // Match Anvil NBT layout used by longArrayToUint32 / readChunkFromRegion.
    longs.push([data[i * 2] << 32 >> 32, data[i * 2 + 1] << 32 >> 32]);
  }
  return longs;
}

/**
 * Build palette index array from state ids; returns { paletteEntries, indices }.
 * @param {number[]} stateIds length 4096
 */
function buildPaletteFromStateIds(stateIds) {
  const map = new Map();
  const indices = new Uint16Array(BLOCK_VOLUME);
  for (let i = 0; i < BLOCK_VOLUME; i++) {
    const sid = stateIds[i] ?? 0;
    let idx = map.get(sid);
    if (idx === undefined) {
      idx = map.size;
      map.set(sid, idx);
    }
    indices[i] = idx;
  }
  const paletteEntries = [...map.keys()];
  return { paletteEntries, indices };
}

module.exports = {
  BLOCK_VOLUME,
  bitsForBlockPalette,
  bitsForBiomePalette,
  palettedStorageLongCount,
  blockIndex,
  getPackedIndex,
  setPackedIndex,
  longArrayToUint32,
  packPaletteIndices,
  buildPaletteFromStateIds,
};

'use strict';

const SmartBuffer = require('smart-buffer').SmartBuffer;
const BitArray = require('prismarine-chunk/src/pc/common/BitArrayNoSpan');
const varInt = require('prismarine-chunk/src/pc/common/varInt');
const { blockIndex, BLOCK_VOLUME } = require('./palettedContainer');

const MAX_BITS_PER_BLOCK = 8;

/**
 * Decode map_chunk chunkData (protocol section stream). Uses wire BitArray packing only.
 * @param {Buffer} data
 * @param {{ minY: number, worldHeight: number }} bounds
 * @returns {Map<number, { solidBlockCount: number, stateIds: number[] }>} sectionY → unpacked state ids
 */
function decodeMapChunkData(data, bounds) {
  const reader = SmartBuffer.fromBuffer(data);
  const numSections = bounds.worldHeight >> 4;
  const sections = new Map();

  for (let i = 0; i < numSections; i++) {
    if (reader.remaining <= 0) break;

    const solidBlockCount = reader.readInt16BE();
    const bitsPerBlock = reader.readUInt8();
    let palette = null;

    if (bitsPerBlock <= MAX_BITS_PER_BLOCK) {
      palette = [];
      const numPaletteItems = varInt.read(reader);
      for (let p = 0; p < numPaletteItems; p++) {
        palette.push(varInt.read(reader));
      }
    }

    const longCount = varInt.read(reader);
    const bitData = new BitArray({
      bitsPerValue: bitsPerBlock > MAX_BITS_PER_BLOCK ? 15 : bitsPerBlock,
      capacity: BLOCK_VOLUME,
    });
    bitData.readBuffer(reader, longCount * 2);

    const stateIds = new Array(BLOCK_VOLUME);
    for (let idx = 0; idx < BLOCK_VOLUME; idx++) {
      let sid = bitData.get(idx);
      if (palette != null) sid = palette[sid];
      stateIds[idx] = sid;
    }

    const sectionY = (bounds.minY >> 4) + i;
    sections.set(sectionY, { solidBlockCount, stateIds });
  }

  return sections;
}

/**
 * Anvil section Y index for a world block Y.
 * @param {number} worldY
 * @param {{ minY: number }} bounds
 */
function sectionYForBlockY(worldY, bounds) {
  return Math.floor((worldY - bounds.minY) / 16) + (bounds.minY >> 4);
}

/**
 * @param {number} worldX
 * @param {number} worldY
 * @param {number} worldZ
 * @param {number} chunkX
 * @param {number} chunkZ
 */
function localCoords(worldX, worldY, worldZ, chunkX, chunkZ) {
  return {
    x: worldX & 15,
    y: worldY & 15,
    z: worldZ & 15,
  };
}

/**
 * @param {Map<number, { stateIds: number[] }>} sections
 * @param {number} chunkX
 * @param {number} chunkZ
 * @param {number} worldX
 * @param {number} worldY
 * @param {number} worldZ
 * @returns {number|null} state id
 */
function getStateIdAt(sections, bounds, worldX, worldY, worldZ) {
  const secY = sectionYForBlockY(worldY, bounds);
  const sec = sections.get(secY);
  if (!sec) return null;
  const { x, y, z } = localCoords(worldX, worldY, worldZ, chunkX, chunkZ);
  return sec.stateIds[blockIndex(x, y, z)] ?? null;
}

/**
 * @param {Map<number, { stateIds: number[] }>} sections
 * @param {number} worldX
 * @param {number} worldY
 * @param {number} worldZ
 * @param {number} stateId
 */
function setStateIdAt(sections, bounds, worldX, worldY, worldZ, stateId) {
  const secY = sectionYForBlockY(worldY, bounds);
  let sec = sections.get(secY);
  if (!sec) {
    sec = { solidBlockCount: 0, stateIds: new Array(BLOCK_VOLUME).fill(0) };
    sections.set(secY, sec);
  }
  const { x, y, z } = localCoords(worldX, worldY, worldZ, 0, 0);
  sec.stateIds[blockIndex(x, y, z)] = stateId;
}

/** Deep-copy section state for cache mutation. */
function cloneWireSections(sections) {
  const out = new Map();
  for (const [y, sec] of sections) {
    out.set(y, {
      solidBlockCount: sec.solidBlockCount,
      stateIds: sec.stateIds.slice(),
    });
  }
  return out;
}

/**
 * @param {Map<number, { stateIds: number[] }>} sections
 * @param {{ location: { x: number, y: number, z: number }, type: number }} packet
 */
function applyBlockChangeWire(sections, packet) {
  const { x, y, z } = packet.location;
  const secY = Math.floor(y / 16);
  const sec = sections.get(secY);
  if (!sec) return;
  const lx = x & 15;
  const ly = y & 15;
  const lz = z & 15;
  sec.stateIds[blockIndex(lx, ly, lz)] = packet.type;
}

/**
 * chunkCoordinates.y is the Anvil section Y index (same as map_chunk section Y tag).
 * @param {Map<number, { stateIds: number[] }>} sections
 * @param {{ chunkCoordinates: { x: number, z: number, y: number }, records: number[] }} packet
 */
function applyMultiBlockChangeWire(sections, packet) {
  const secY = packet.chunkCoordinates?.y;
  if (secY == null) return;
  const sec = sections.get(secY);
  if (!sec || !packet.records?.length) return;

  for (const record of packet.records) {
    const blockZ = (record >> 4) & 0x0f;
    const blockX = (record >> 8) & 0x0f;
    const blockY = record & 0x0f;
    const stateId = record >>> 12;
    sec.stateIds[blockIndex(blockX, blockY, blockZ)] = stateId;
  }
}

/**
 * Apply merged wire section state ids onto a prismarine column (for reference Anvil encode).
 * @param {import('prismarine-chunk').Chunk} column
 * @param {Map<number, { stateIds: number[] }>} wireSections
 * @param {{ minY: number }} bounds
 */
function applyWireSectionsToColumn(column, wireSections, bounds) {
  for (const [sectionY, sec] of wireSections) {
    for (let ly = 0; ly < 16; ly++) {
      for (let lz = 0; lz < 16; lz++) {
        for (let lx = 0; lx < 16; lx++) {
          const idx = blockIndex(lx, ly, lz);
          const stateId = sec.stateIds[idx];
          if (stateId == null) continue;
          const worldY = sectionY * 16 + ly;
          column.setBlockStateId({ x: lx, y: worldY, z: lz }, stateId);
        }
      }
    }
  }
}

module.exports = {
  decodeMapChunkData,
  cloneWireSections,
  getStateIdAt,
  setStateIdAt,
  applyBlockChangeWire,
  applyMultiBlockChangeWire,
  applyWireSectionsToColumn,
  localCoords,
  sectionYForBlockY,
};

'use strict';

const fs = require('fs');
const zlib = require('zlib');
const nbt = require('prismarine-nbt');

const SECTOR_BYTES = 4096;

/**
 * @param {object} chunkTag - sanitized compound tag (.value map)
 * @returns {Buffer}
 */
function encodeChunkPayload(chunkTag) {
  const payload = nbt.writeUncompressed({ type: 'compound', name: '', value: chunkTag });
  return zlib.gzipSync(payload, { level: zlib.constants.Z_BEST_SPEED });
}

/** Vanilla entity regions use zlib (compression type 2). */
function encodeEntityChunkPayload(chunkTag) {
  const payload = nbt.writeUncompressed({ type: 'compound', name: '', value: chunkTag });
  return zlib.deflateSync(payload, { level: zlib.constants.Z_BEST_SPEED });
}

/**
 * Load or allocate a region file buffer.
 * @param {string} regionPath
 */
function loadRegionBuffer(regionPath) {
  if (fs.existsSync(regionPath)) {
    return fs.readFileSync(regionPath);
  }
  const buf = Buffer.alloc(SECTOR_BYTES * 2);
  buf.fill(0);
  return buf;
}

/**
 * Patch one gzip chunk payload into an in-memory region buffer.
 * @param {Buffer} buf
 * @param {number} chunkX
 * @param {number} chunkZ
 * @param {Buffer} payload
 * @returns {Buffer}
 */
function patchRegionBuffer(buf, chunkX, chunkZ, payload, compressionType = 1) {
  const sectorCount = Math.ceil((5 + payload.length) / SECTOR_BYTES);
  const totalSize = sectorCount * SECTOR_BYTES;

  const localX = chunkX & 31;
  const localZ = chunkZ & 31;
  const index = localX + localZ * 32;
  const indexOffset = index * 4;

  const oldOffset = buf.readUInt32BE(indexOffset);
  const oldSector = oldOffset >>> 8;
  const oldCount = oldOffset & 0xff;

  let writeSector;
  if (oldSector && oldCount >= sectorCount) {
    writeSector = oldSector;
  } else {
    const usedEnd = Math.max(2, Math.ceil(buf.length / SECTOR_BYTES));
    writeSector = usedEnd;
    const needed = (writeSector + sectorCount) * SECTOR_BYTES;
    if (buf.length < needed) {
      const grown = Buffer.alloc(needed);
      buf.copy(grown);
      buf = grown;
    }
    const sectorOffset = (writeSector << 8) | sectorCount;
    buf.writeUInt32BE(sectorOffset, indexOffset);
  }

  const sectorOffset = writeSector * SECTOR_BYTES;
  buf.writeUInt32BE(payload.length + 1, sectorOffset);
  buf.writeUInt8(compressionType, sectorOffset + 4);
  payload.copy(buf, sectorOffset + 5);

  if (buf.length < sectorOffset + totalSize) {
    const grown = Buffer.alloc(sectorOffset + totalSize);
    buf.copy(grown);
    buf = grown;
  }

  return buf;
}

module.exports = {
  encodeChunkPayload,
  encodeEntityChunkPayload,
  loadRegionBuffer,
  patchRegionBuffer,
  SECTOR_BYTES,
};

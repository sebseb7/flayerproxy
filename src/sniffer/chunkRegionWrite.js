'use strict';

const {
  worldBoundsForDimension,
  normalizeMapChunkPacket,
} = require('../state/chunkMerge');
const { mapChunkToReferenceAnvilTag } = require('./mapChunkReferenceExport');
const { sanitizeTagForWrite } = require('./anvilBlockEntity');
const { registryVersionFor } = require('./minecraftRegistry');

/**
 * Encode merged map_chunk + column to a sanitized Anvil chunk compound (.value map).
 * @param {object} packetData
 * @param {import('prismarine-chunk').Chunk|null} [column]
 * @param {{ version: string, dimensionName?: string, worldBounds?: object }} opts
 */
function encodeReferenceChunkTag(packetData, column, opts) {
  const registryVersion = registryVersionFor(opts.version);
  const bounds =
    opts.worldBounds ??
    worldBoundsForDimension(opts.version, opts.dimensionName);
  const packet = normalizeMapChunkPacket(packetData);
  const rawTag = mapChunkToReferenceAnvilTag(
    packet,
    bounds,
    registryVersion,
    column ?? null,
  );
  return sanitizeTagForWrite({ type: 'compound', value: rawTag })?.value ?? rawTag;
}

module.exports = { encodeReferenceChunkTag };

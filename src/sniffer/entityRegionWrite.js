'use strict';

const { entityChunkTagFromEntities } = require('./entityAnvilEncode');

/**
 * @param {{ x: number, z: number, entities: object[] }} entry
 * @param {{ version: string, dimensionName?: string, worldBounds?: object }} opts
 */
function encodeEntityRegionChunkTag(entry, opts) {
  return entityChunkTagFromEntities(
    entry.x,
    entry.z,
    entry.entities ?? [],
    {
      version: opts.version,
      lookupEntity: entry.lookupEntity,
    },
  );
}

module.exports = { encodeEntityRegionChunkTag };

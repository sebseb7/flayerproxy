'use strict';

const nbt = require('prismarine-nbt');
const {
  BLOCK_ENTITY_TYPE_BY_ID,
  anvilBlockEntityId,
  stubBlockEntityValue,
  ensureVanillaBlockEntityShell,
} = require('./anvilBlockEntity');
const { parseBlockEntityNbtPayload } = require('./packetNbt');
const { sectionYForBlockY } = require('./mapChunkWire');

/**
 * Resolve entity id from block at position (wire state) or protocol type id.
 * @param {Map<number, { stateIds: number[] }>|null} wireSections
 * @param {import('minecraft-data').IndexedData} mcData
 * @param {{ minY: number }} bounds
 * @param {number} chunkX
 * @param {number} chunkZ
 * @param {{ x: number, y: number, z: number }} local - chunk-local block entity coords from packet
 * @param {number} [typeId]
 */
function resolveEntityId(wireSections, mcData, bounds, chunkX, chunkZ, local, typeId) {
  let blockName = 'air';
  if (wireSections) {
    const secY = sectionYForBlockY(local.y, bounds);
    const sec = wireSections.get(secY);
    if (sec) {
      const idx = (local.y & 15) << 8 | (local.z & 15) << 4 | (local.x & 15);
      const sid = sec.stateIds[idx] ?? 0;
      blockName = mcData.blocksByStateId[sid]?.name ?? 'air';
    }
  }
  if (blockName && blockName !== 'air') {
    const fromBlock = anvilBlockEntityId(blockName.replace(/^minecraft:/, ''));
    if (fromBlock) return fromBlock;
  }
  return BLOCK_ENTITY_TYPE_BY_ID[typeId] ?? null;
}

/**
 * Upsert a block entity on map_chunk packetData.blockEntities (protocol shape).
 * @param {object} packetData
 * @param {object} tilePacket - tile_entity_data
 * @param {number} chunkX
 * @param {number} chunkZ
 * @param {Map<number, { stateIds: number[] }>|null} wireSections
 * @param {{ minY: number }} bounds
 * @param {import('minecraft-data').IndexedData} mcData
 */
function applyTileEntityToPacket(
  packetData,
  tilePacket,
  chunkX,
  chunkZ,
  wireSections,
  bounds,
  mcData,
) {
  const loc = tilePacket.location;
  if (!loc || loc.x == null) return;

  const local = { x: loc.x & 15, y: loc.y, z: loc.z & 15 };
  const entityId = resolveEntityId(
    wireSections,
    mcData,
    bounds,
    chunkX,
    chunkZ,
    local,
    tilePacket.action,
  );
  if (!entityId) return;

  if (!packetData.blockEntities) packetData.blockEntities = [];

  const existing = packetData.blockEntities.find(
    (e) => e.x === local.x && e.y === local.y && e.z === local.z,
  );

  let value = parseBlockEntityNbtPayload(tilePacket.nbtData);
  if (!value) value = stubBlockEntityValue(entityId);
  value.x = nbt.int(loc.x);
  value.y = nbt.int(loc.y);
  value.z = nbt.int(loc.z);
  if (!value.id) value.id = nbt.string(entityId);
  ensureVanillaBlockEntityShell(value);

  if (existing) {
    existing.nbtData = { type: 'compound', value };
    const typeEntry = Object.entries(BLOCK_ENTITY_TYPE_BY_ID).find(([, id]) => id === entityId);
    if (typeEntry) existing.type = Number(typeEntry[0]);
  } else {
    const typeEntry = Object.entries(BLOCK_ENTITY_TYPE_BY_ID).find(([, id]) => id === entityId);
    packetData.blockEntities.push({
      x: local.x,
      y: local.y,
      z: local.z,
      type: typeEntry ? Number(typeEntry[0]) : 0,
      nbtData: { type: 'compound', value },
    });
  }
}

module.exports = { applyTileEntityToPacket, resolveEntityId };

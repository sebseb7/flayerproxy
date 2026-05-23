'use strict';

const nbt = require('prismarine-nbt');
const { loadColumnFromMapChunk } = require('../state/chunkMerge');
const { createChunkAnvilEncoder, listOfCompounds } = require('./chunkAnvilEncode');
const {
  normalizeBlockEntityForReference,
  referenceEntityIdFromType,
  stubBlockEntityValue,
  ensureVanillaBlockEntityShell,
} = require('./anvilBlockEntity');
const { parseBlockEntityNbtPayload } = require('./packetNbt');

/** map_chunk heightmap type → Anvil compound key (session-1779506450054-2 layout). */
const HEIGHTMAP_TYPE_TO_ANVIL = {
  motion_blocking: 'MOTION_BLOCKING',
  world_surface: 'WORLD_SURFACE',
  ocean_floor: 'OCEAN_FLOOR',
  ocean_floor_wg: 'OCEAN_FLOOR',
  motion_blocking_no_leaves: 'MOTION_BLOCKING_NO_LEAVES',
  world_surface_wg: 'WORLD_SURFACE',
};

/**
 * @param {object} packet
 * @returns {object|null}
 */
function heightmapsTagFromPacket(packet) {
  const list = packet.heightmaps;
  if (!list?.length) return null;

  const value = {};
  for (const hm of list) {
    const key = HEIGHTMAP_TYPE_TO_ANVIL[hm.type] ?? String(hm.type).toUpperCase();
    if (hm.data?.length) value[key] = nbt.longArray(hm.data);
  }

  if (value.MOTION_BLOCKING && !value.MOTION_BLOCKING_NO_LEAVES) {
    value.MOTION_BLOCKING_NO_LEAVES = value.MOTION_BLOCKING;
  }
  if (value.MOTION_BLOCKING && !value.OCEAN_FLOOR) {
    value.OCEAN_FLOOR = value.MOTION_BLOCKING;
  }

  if (!Object.keys(value).length) return null;
  return nbt.comp(value);
}

/**
 * Light-only sections (e.g. Y=-5) present in reference saves.
 * @param {import('prismarine-chunk').Chunk} column
 */
function appendReferenceLightSections(column) {
  const tags = [];
  const minSectionY = column.minY >> 4;

  for (let si = 0; si < column.skyLightSections.length; si++) {
    const blockIdx = si - 1;
    const sectionY = minSectionY + blockIdx;
    if (blockIdx >= 0 && column.sections[blockIdx]) continue;

    const sky = column.skyLightSections[si];
    const isEmpty = column.emptySkyLightMask?.get?.(si);
    if (!sky || !isEmpty) continue;

    const skyArr =
      sky.bitsPerValue === 4
        ? new Int8Array(sky.data.buffer)
        : sky.resizeTo(4);
    tags.push({
      Y: nbt.byte(sectionY),
      SkyLight: nbt.byteArray(skyArr),
    });
  }

  return tags;
}

/**
 * @param {object} packet
 * @param {number} chunkX
 * @param {number} chunkZ
 */
function blockEntitiesFromPacket(packet, chunkX, chunkZ) {
  const tags = [];
  for (const entry of packet.blockEntities ?? []) {
    if (entry.x === undefined) continue;
    const entityId = referenceEntityIdFromType(entry.type);
    if (!entityId) continue;

    const wx = chunkX * 16 + (entry.x & 15);
    const wy = entry.y;
    const wz = chunkZ * 16 + (entry.z & 15);

    let value = parseBlockEntityNbtPayload(entry.nbtData);
    if (!value) value = stubBlockEntityValue(entityId);
    value.x = nbt.int(wx);
    value.y = nbt.int(wy);
    value.z = nbt.int(wz);
    if (!value.id) value.id = nbt.string(entityId);
    ensureVanillaBlockEntityShell(value);
    tags.push(normalizeBlockEntityForReference(value, entityId));
  }
  return tags;
}

/**
 * Export in reference (-2) Anvil layout: prismarine section encode, wire terrain, packet BEs.
 *
 * @param {object} packet
 * @param {{ minY: number, worldHeight: number }} bounds
 * @param {string} version
 * @param {import('prismarine-chunk').Chunk} [column] - merged column (map_chunk + block_change / multi_block_change)
 */
function mapChunkToReferenceAnvilTag(packet, bounds, version, column) {
  const chunkX = packet.x ?? 0;
  const chunkZ = packet.z ?? 0;
  let merged = column;
  if (
    merged &&
    (merged.minY !== bounds.minY || merged.worldHeight !== bounds.worldHeight)
  ) {
    merged = null;
  }
  merged = merged ?? loadColumnFromMapChunk(packet, version, bounds);

  const { columnToAnvilChunkTag } = createChunkAnvilEncoder(version);
  const blockEntities = blockEntitiesFromPacket(packet, chunkX, chunkZ);
  const tag = columnToAnvilChunkTag(merged, chunkX, chunkZ, {
    referenceFormat: true,
    skipSynthesize: true,
    blockEntities,
  });

  const value = tag?.value ?? tag;
  const hm = heightmapsTagFromPacket(packet);
  if (hm) value.Heightmaps = hm;
  value.isLightOn = nbt.byte(
    packet.skyLight?.length || packet.blockLight?.length ? 1 : 0,
  );

  const lightOnly = appendReferenceLightSections(merged);
  if (lightOnly.length && value.sections) {
    const listInner = value.sections.value;
    const existing = Array.isArray(listInner)
      ? listInner
      : Array.isArray(listInner?.value)
        ? listInner.value
        : [];
    const combined = [...existing, ...lightOnly];
    combined.sort((a, b) => (a.Y?.value ?? a.Y) - (b.Y?.value ?? b.Y));
    value.sections = listOfCompounds(combined);
  }

  return value;
}

module.exports = {
  mapChunkToReferenceAnvilTag,
  heightmapsTagFromPacket,
  blockEntitiesFromPacket,
  HEIGHTMAP_TYPE_TO_ANVIL,
};

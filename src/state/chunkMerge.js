const zlib = require('zlib');
const ChunkLoader = require('prismarine-chunk');
const nbt = require('prismarine-nbt');

const DEFAULT_WORLD = { minY: -64, worldHeight: 384 };

/** Vanilla vertical bounds when registry lookup is unavailable (e.g. 1.21.x). */
const DIMENSION_BOUNDS = {
  overworld: { minY: -64, worldHeight: 384 },
  the_nether: { minY: 0, worldHeight: 256 },
  the_end: { minY: 0, worldHeight: 256 },
};

/**
 * prismarine-chunk uses smart-buffer, which requires Node Buffers.
 * structuredClone (and some decoders) produce Uint8Array instead.
 * @param {Buffer|Uint8Array|number[]|null|undefined} value
 * @returns {Buffer|null}
 */
function asBuffer(value) {
  if (value == null) return null;
  if (Buffer.isBuffer(value)) return value;
  if (value instanceof Uint8Array || Array.isArray(value)) return Buffer.from(value);
  return null;
}

/**
 * Ensure map_chunk binary fields are Buffers after structuredClone / protocol decode.
 * @param {object} packet
 */
function normalizeMapChunkPacket(packet) {
  const chunkData = packet.chunkData ?? packet.data;
  const buf = asBuffer(chunkData);
  if (buf) {
    if (packet.chunkData !== undefined) packet.chunkData = buf;
    else packet.data = buf;
  }
  for (const key of ['skyLight', 'blockLight']) {
    if (!Array.isArray(packet[key])) continue;
    packet[key] = packet[key].map((section) => asBuffer(section) ?? section);
  }
  return packet;
}

/**
 * Params ready for proxy client serializer (one chunkData field, Buffers normalized).
 * @param {object} packetData
 */
function prepareMapChunkParams(packetData) {
  const params = normalizeMapChunkPacket(structuredClone(packetData));
  if (params.data !== undefined) {
    if (params.chunkData === undefined) {
      params.chunkData = params.data;
    }
    delete params.data;
  }
  return params;
}

/**
 * World bounds for prismarine-chunk from login dimension (defaults to overworld).
 * @param {string} version
 * @param {string} [dimensionName]
 */
function worldBoundsForDimension(version, dimensionName = 'overworld') {
  const key = String(dimensionName).replace(/^minecraft:/, '');
  const known = DIMENSION_BOUNDS[key];
  if (known) {
    return { ...known };
  }

  try {
    const registry = require('prismarine-registry')(version);
    const dim =
      registry.dimensionsByName?.[key] ?? registry.dimensionsByName?.[dimensionName];
    if (dim) {
      return { minY: dim.minY, worldHeight: dim.height };
    }
  } catch {
    /* fall through */
  }
  return { ...DEFAULT_WORLD };
}

/**
 * @param {{ name?: string, dimension?: string|number }|null|undefined} worldState
 * @returns {string|null}
 */
function dimensionNameFromWorldState(worldState) {
  if (!worldState) return null;
  if (typeof worldState.name === 'string') {
    return worldState.name.replace(/^minecraft:/, '');
  }
  if (worldState.dimension != null) {
    return dimensionNameFromLogin({ dimension: worldState.dimension });
  }
  return null;
}

/**
 * @param {object} loginPacket
 * @returns {string}
 */
function dimensionNameFromLogin(loginPacket) {
  const fromState = dimensionNameFromWorldState(loginPacket?.worldState);
  if (fromState) return fromState;
  if (!loginPacket?.dimension) return 'overworld';
  const d = loginPacket.dimension;
  if (typeof d === 'string') return d.replace(/^minecraft:/, '');
  const names = ['the_nether', 'overworld', 'the_end'];
  return names[d] ?? 'overworld';
}

const { anvilBlockEntityId, stubBlockEntityValue } = require('../sniffer/anvilBlockEntity');

/**
 * @param {import('prismarine-chunk').Chunk} column
 * @param {{ x: number, y: number, z: number }} localPos
 * @param {number} [typeId] - map_chunk blockEntities[].type
 */
function resolveBlockEntityId(column, localPos, typeId) {
  const block = column.getBlock(localPos);
  const blockName = block?.name?.replace(/^minecraft:/, '');
  return anvilBlockEntityId(blockName, typeId);
}

/**
 * Block entity id for Anvil from the block at this chunk-local position.
 * @param {import('prismarine-chunk').Chunk} column
 * @param {{ x: number, y: number, z: number }} localPos
 */
function blockEntityIdFromBlock(column, localPos) {
  return resolveBlockEntityId(column, localPos);
}

/**
 * map_chunk / tile_entity_data NBT may be a Buffer or a protocol compound tree.
 * @param {Buffer|Uint8Array|object|null|undefined} nbtData
 * @returns {Record<string, object>|null}
 */
function parseBlockEntityNbtPayload(nbtData) {
  if (nbtData == null) return null;
  if (nbtData.type === 'compound') {
    if (!nbtData.value || !Object.keys(nbtData.value).length) return null;
    return { ...nbtData.value };
  }
  const buf = asBuffer(nbtData);
  if (!buf?.length) return null;
  const attempts = [buf];
  try {
    attempts.push(zlib.gunzipSync(buf));
  } catch {
    /* not gzipped */
  }
  for (const data of attempts) {
    try {
      const root = nbt.parseUncompressed(data);
      if (root?.type === 'compound' && root.value) return { ...root.value };
    } catch {
      /* try next */
    }
  }
  return null;
}

/**
 * @param {import('prismarine-chunk').Chunk} column
 * @param {object} blockEntity - map_chunk blockEntities[] entry
 * @param {number} chunkX
 * @param {number} chunkZ
 */
function applyMapChunkBlockEntity(column, blockEntity, chunkX, chunkZ) {
  if (blockEntity.x === undefined) return;

  const pos = {
    x: blockEntity.x & 0xf,
    y: blockEntity.y,
    z: blockEntity.z & 0xf,
  };
  const entityId = resolveBlockEntityId(column, pos, blockEntity.type);
  let value = parseBlockEntityNbtPayload(blockEntity.nbtData);
  if (!value) {
    if (!entityId) return;
    value = stubBlockEntityValue(entityId);
  }
  value.x = nbt.int(chunkX * 16 + pos.x);
  value.y = nbt.int(blockEntity.y);
  value.z = nbt.int(chunkZ * 16 + pos.z);
  const resolvedId = entityId ?? blockEntityIdFromBlock(column, pos);
  if (!resolvedId) return;
  if (!value.id) {
    value.id = nbt.string(resolvedId);
  }
  column.setBlockEntity(pos, { type: 'compound', name: '', value });
}

/**
 * Late sign/chest updates after the chunk was sent.
 * @param {import('prismarine-chunk').Chunk} column
 * @param {object} packet - tile_entity_data
 * @param {number} chunkX
 * @param {number} chunkZ
 */
function applyTileEntityData(column, packet, chunkX, chunkZ) {
  const loc = packet.location;
  if (!loc || loc.x == null) return;
  if (chunkX !== Math.floor(loc.x / 16) || chunkZ !== Math.floor(loc.z / 16)) return;

  const pos = { x: loc.x & 0xf, y: loc.y, z: loc.z & 0xf };
  const entityId = resolveBlockEntityId(column, pos, packet.action);
  let value = parseBlockEntityNbtPayload(packet.nbtData);
  if (!value) {
    if (!entityId) return;
    value = stubBlockEntityValue(entityId);
  }
  value.x = nbt.int(loc.x);
  value.y = nbt.int(loc.y);
  value.z = nbt.int(loc.z);
  const resolvedId = entityId ?? blockEntityIdFromBlock(column, pos);
  if (!resolvedId) return;
  if (!value.id) {
    value.id = nbt.string(resolvedId);
  }
  column.setBlockEntity(pos, { type: 'compound', name: '', value });
}

/**
 * @param {string} version
 * @param {{ minY: number, worldHeight: number }} worldBounds
 * @returns {import('prismarine-chunk').Chunk}
 */
function createEmptyColumn(version, worldBounds) {
  const Chunk = ChunkLoader(version);
  return new Chunk(worldBounds);
}

/**
 * @param {object} packet - decoded map_chunk
 * @param {string} version
 * @param {{ minY: number, worldHeight: number }} worldBounds
 */
function loadColumnFromMapChunk(packet, version, worldBounds) {
  const Chunk = ChunkLoader(version);
  const column = new Chunk(worldBounds);
  normalizeMapChunkPacket(packet);
  const chunkData = asBuffer(packet.chunkData ?? packet.data);
  if (chunkData?.length) {
    column.load(chunkData);
  }
  if (packet.skyLight !== undefined) {
    column.loadParsedLight(
      packet.skyLight,
      packet.blockLight,
      packet.skyLightMask,
      packet.blockLightMask,
      packet.emptySkyLightMask,
      packet.emptyBlockLightMask
    );
  }
  if (packet.blockEntities?.length) {
    const chunkX = packet.x ?? 0;
    const chunkZ = packet.z ?? 0;
    for (const blockEntity of packet.blockEntities) {
      try {
        applyMapChunkBlockEntity(column, blockEntity, chunkX, chunkZ);
      } catch {
        /* skip malformed block entity payload */
      }
    }
  }
  return column;
}

/**
 * @param {import('prismarine-chunk').Chunk} column
 * @param {object} packet - decoded map_chunk template (heightmaps, blockEntities, x, z preserved)
 */
function exportMapChunkPacket(column, packet) {
  const out = structuredClone(packet);
  delete out.data;
  out.chunkData = column.dump();
  const light = column.dumpLight();
  out.skyLight = light.skyLight;
  out.blockLight = light.blockLight;
  out.skyLightMask = light.skyLightMask;
  out.blockLightMask = light.blockLightMask;
  out.emptySkyLightMask = light.emptySkyLightMask;
  out.emptyBlockLightMask = light.emptyBlockLightMask;
  return normalizeMapChunkPacket(out);
}

/**
 * Chunk columns use local X/Z (0–15) and world Y. See prismarine-world posInChunk.
 * @param {import('prismarine-chunk').Chunk} column
 * @param {{ location: { x: number, y: number, z: number }, type: number }} packet
 */
function applyBlockChange(column, packet) {
  const { x, y, z } = packet.location;
  column.setBlockStateId({ x: x & 0x0f, y, z: z & 0x0f }, packet.type);
}

/**
 * @param {import('prismarine-chunk').Chunk} column
 * @param {object} packet - decoded update_light (same light fields as map_chunk)
 */
function applyUpdateLight(column, packet) {
  column.loadParsedLight(
    packet.skyLight ?? [],
    packet.blockLight ?? [],
    packet.skyLightMask,
    packet.blockLightMask,
    packet.emptySkyLightMask,
    packet.emptyBlockLightMask
  );
}

/**
 * @param {import('prismarine-chunk').Chunk} column
 * @param {{ chunkCoordinates?: { x: number, y: number, z: number }, records: number[] }} packet
 */
function applyMultiBlockChange(column, packet) {
  const coords = packet.chunkCoordinates;
  if (!coords || !packet.records?.length) return;
  const sectionY = coords.y;

  for (const record of packet.records) {
    const blockZ = (record >> 4) & 0x0f;
    const blockX = (record >> 8) & 0x0f;
    const blockY = record & 0x0f;
    const stateId = record >> 12;
    column.setBlockStateId(
      {
        x: blockX,
        y: sectionY * 16 + blockY,
        z: blockZ,
      },
      stateId
    );
  }
}

module.exports = {
  DEFAULT_WORLD,
  DIMENSION_BOUNDS,
  asBuffer,
  normalizeMapChunkPacket,
  prepareMapChunkParams,
  worldBoundsForDimension,
  dimensionNameFromLogin,
  dimensionNameFromWorldState,
  blockEntityIdFromBlock,
  resolveBlockEntityId,
  stubBlockEntityValue,
  parseBlockEntityNbtPayload,
  applyMapChunkBlockEntity,
  applyTileEntityData,
  createEmptyColumn,
  loadColumnFromMapChunk,
  exportMapChunkPacket,
  applyBlockChange,
  applyUpdateLight,
  applyMultiBlockChange,
};

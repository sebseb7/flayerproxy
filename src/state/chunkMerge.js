const ChunkLoader = require('prismarine-chunk');

const DEFAULT_WORLD = { minY: -64, worldHeight: 384 };

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
 * World bounds for prismarine-chunk from login dimension (defaults to overworld).
 * @param {string} version
 * @param {string} [dimensionName]
 */
function worldBoundsForDimension(version, dimensionName = 'overworld') {
  try {
    const registry = require('prismarine-registry')(version);
    const dim =
      registry.dimensionsByName[dimensionName] ??
      registry.dimensionsByName[dimensionName.replace(/^minecraft:/, '')];
    if (dim) {
      return { minY: dim.minY, worldHeight: dim.height };
    }
  } catch {
    /* fall through */
  }
  return { ...DEFAULT_WORLD };
}

/**
 * @param {object} loginPacket
 * @returns {string}
 */
function dimensionNameFromLogin(loginPacket) {
  if (!loginPacket?.dimension) return 'overworld';
  const d = loginPacket.dimension;
  if (typeof d === 'string') return d.replace(/^minecraft:/, '');
  const names = ['the_nether', 'overworld', 'the_end'];
  return names[d] ?? 'overworld';
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
    for (const blockEntity of packet.blockEntities) {
      if (blockEntity.x !== undefined) {
        column.setBlockEntity(
          { x: blockEntity.x & 0xf, y: blockEntity.y, z: blockEntity.z & 0xf },
          blockEntity
        );
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
  asBuffer,
  normalizeMapChunkPacket,
  worldBoundsForDimension,
  dimensionNameFromLogin,
  createEmptyColumn,
  loadColumnFromMapChunk,
  exportMapChunkPacket,
  applyBlockChange,
  applyUpdateLight,
  applyMultiBlockChange,
};

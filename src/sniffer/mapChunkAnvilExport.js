'use strict';

const nbt = require('prismarine-nbt');
const {
  BLOCK_ENTITY_TYPE_BY_ID,
  anvilBlockEntityId,
  stubBlockEntityValue,
  normalizeBlockEntityForAnvil,
  ensureVanillaBlockEntityShell,
} = require('./anvilBlockEntity');
const { asBuffer, parseBlockEntityNbtPayload } = require('./packetNbt');
const { decodeMapChunkData, sectionYForBlockY } = require('./mapChunkWire');
const {
  bitsForBlockPalette,
  bitsForBiomePalette,
  packPaletteIndices,
  buildPaletteFromStateIds,
  BLOCK_VOLUME,
} = require('./palettedContainer');
const { listOfCompounds, listOfStrings } = require('./chunkAnvilEncode');

/**
 * @param {number} typeId
 * @param {import('minecraft-data').IndexedData} mcData
 * @param {Map<number, { stateIds: number[] }>} sections
 * @param {number} chunkX
 * @param {number} chunkZ
 * @param {object} entry - map_chunk blockEntities[] item
 */
function entityIdForEntry(typeId, mcData, sections, chunkX, chunkZ, entry, bounds) {
  const wx = chunkX * 16 + (entry.x & 15);
  const wy = entry.y;
  const wz = chunkZ * 16 + (entry.z & 15);
  const sec = sections.get(sectionYForBlockY(wy, bounds));
  let blockName = 'air';
  if (sec) {
    const lx = entry.x & 15;
    const ly = wy & 15;
    const lz = entry.z & 15;
    const idx = (ly << 8) | (lz << 4) | lx;
    const sid = sec.stateIds[idx] ?? 0;
    blockName = mcData.blocksByStateId[sid]?.name ?? 'air';
  }
  if (blockName && blockName !== 'air') {
    const fromBlock = anvilBlockEntityId(blockName.replace(/^minecraft:/, ''));
    if (fromBlock) return fromBlock;
  }
  return BLOCK_ENTITY_TYPE_BY_ID[typeId] ?? null;
}

/**
 * @param {number[]} stateIds
 * @param {import('minecraft-data').IndexedData} mcData
 */
function sectionTagFromStateIds(stateIds, Block, sectionY) {
  const { paletteEntries, indices } = buildPaletteFromStateIds(stateIds);
  const names = paletteEntries.map((sid) => {
    const block = Block.fromStateId(sid);
    const entry = { Name: nbt.string(`minecraft:${block.name}`) };
    const props = block.getProperties();
    if (props && Object.keys(props).length) {
      entry.Properties = nbt.comp(
        Object.fromEntries(
          Object.entries(props).map(([k, v]) => [k, nbt.string(String(v))]),
        ),
      );
    }
    return entry;
  });

  const bitsPerBlock = bitsForBlockPalette(names.length);
  let blockStates;
  if (!bitsPerBlock) {
    blockStates = nbt.comp({ palette: listOfCompounds(names) });
  } else {
    blockStates = nbt.comp({
      palette: listOfCompounds(names),
      data: nbt.longArray(packPaletteIndices(indices, bitsPerBlock)),
    });
  }

  const biomeNames = ['minecraft:plains'];
  const bitsPerBiome = bitsForBiomePalette(biomeNames.length);
  const biomeIndices = new Uint16Array(BLOCK_VOLUME / 64);
  let biomes;
  if (!bitsPerBiome) {
    biomes = nbt.comp({ palette: listOfStrings(biomeNames) });
  } else {
    biomes = nbt.comp({
      palette: listOfStrings(biomeNames),
      data: nbt.longArray(packPaletteIndices(biomeIndices, bitsPerBiome)),
    });
  }

  return {
    Y: nbt.byte(sectionY),
    block_states: blockStates,
    biomes,
  };
}

/**
 * @param {object} packet - map_chunk params
 * @param {{ minY: number, worldHeight: number }} bounds
 * @param {string} version - minecraft-data version
 * @param {Map<number, { stateIds: number[] }>} [wireSections] - terrain after block_change / multi_block_change merges
 */
function mapChunkPacketToAnvilTag(packet, bounds, version, wireSections) {
  const mcData = require('minecraft-data')(version);
  const registry = require('prismarine-registry')(version);
  const Block = require('prismarine-block')(registry);
  const dataVersion = mcData.version.dataVersion;
  const chunkX = packet.x ?? 0;
  const chunkZ = packet.z ?? 0;

  const chunkData = asBuffer(packet.chunkData ?? packet.data);
  const sections =
    wireSections ??
    (chunkData?.length ? decodeMapChunkData(chunkData, bounds) : new Map());

  const blockEntityTags = [];

  for (const entry of packet.blockEntities ?? []) {
    if (entry.x === undefined) continue;
    const wx = chunkX * 16 + (entry.x & 15);
    const wy = entry.y;
    const wz = chunkZ * 16 + (entry.z & 15);
    const entityId = entityIdForEntry(entry.type, mcData, sections, chunkX, chunkZ, entry, bounds);
    if (!entityId) continue;

    let value = parseBlockEntityNbtPayload(entry.nbtData);
    if (!value) value = stubBlockEntityValue(entityId);
    value.x = nbt.int(wx);
    value.y = nbt.int(wy);
    value.z = nbt.int(wz);
    if (!value.id) value.id = nbt.string(entityId);
    ensureVanillaBlockEntityShell(value);
    blockEntityTags.push(normalizeBlockEntityForAnvil(value));
  }

  const sectionTags = [];
  for (const [sectionY, sec] of sections) {
    sectionTags.push(sectionTagFromStateIds(sec.stateIds, Block, sectionY));
  }
  sectionTags.sort((a, b) => (a.Y.value ?? a.Y) - (b.Y.value ?? b.Y));

  return {
    DataVersion: nbt.int(dataVersion),
    Status: nbt.string('minecraft:full'),
    xPos: nbt.int(chunkX),
    yPos: nbt.int(bounds.minY >> 4),
    zPos: nbt.int(chunkZ),
    block_entities: listOfCompounds(blockEntityTags),
    LastUpdate: nbt.long([Date.now() & 0xffff, 0]),
    InhabitedTime: nbt.long([0, 0]),
    structures: nbt.comp({}),
    Heightmaps: nbt.comp({}),
    sections: listOfCompounds(sectionTags),
    isLightOn: nbt.byte(0),
    block_ticks: nbt.list({ type: 'end', value: [] }),
    PostProcessing: nbt.list({ type: 'end', value: [] }),
    fluid_ticks: nbt.list({ type: 'end', value: [] }),
  };
}

module.exports = {
  mapChunkPacketToAnvilTag,
  entityIdForEntry,
};

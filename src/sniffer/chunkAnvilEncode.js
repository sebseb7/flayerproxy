'use strict';

const nbt = require('prismarine-nbt');
const {
  normalizeBlockEntityForAnvil,
  normalizeBlockEntityForReference,
  sanitizeTagForWrite,
  synthesizeMissingBlockEntities,
} = require('./anvilBlockEntity');

/**
 * Anvil list-of-compound: each entry is a plain name→tag map (not wrapped in nbt.comp).
 * prismarine-provider-anvil uses nbt.list(nbt.comp(array)) which breaks on write.
 */
function listOfCompounds(items) {
  if (!items?.length) {
    return nbt.list({ type: 'end', value: [] });
  }
  const entries = items.map((item) => {
    if (item?.type === 'compound' && item.value != null) return item.value;
    return item;
  });
  return nbt.list({ type: 'compound', value: entries });
}

const BLOCK_SECTION_VOLUME = 16 * 16 * 16;
const BIOME_SECTION_VOLUME = BLOCK_SECTION_VOLUME / 64;

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

/**
 * Vanilla PalettedContainer long count (matches prismarine BitArray buffer size).
 * NOT ceil(capacity * bits / 64) — e.g. 5 bits → 342 longs, not 320.
 */
function palettedStorageLongCount(capacity, bitsPerValue) {
  return Math.ceil(capacity / Math.floor(64 / bitsPerValue));
}

/**
 * @param {import('prismarine-chunk').Chunk['sections'][0]['data']['data']} bitArray
 * @param {number} bitsPerValue - bits implied by the written palette (vanilla reads this)
 * @param {number} capacity
 */
function packPalettedDataLongArray(bitArray, bitsPerValue, capacity) {
  if (!bitsPerValue) return null;
  const packed =
    bitArray.bitsPerValue === bitsPerValue
      ? bitArray
      : bitArray.resizeTo(bitsPerValue);
  const longCount = palettedStorageLongCount(capacity, bitsPerValue);
  const longs = packed.toLongArray();
  if (longs.length !== longCount) {
    throw new Error(
      `PalettedContainer long mismatch: got ${longs.length} expected ${longCount} (capacity=${capacity} bits=${bitsPerValue})`,
    );
  }
  return nbt.longArray(longs);
}

/** @param {string[]} strings — Anvil biome palettes use plain strings in the list payload */
function listOfStrings(strings) {
  if (!strings?.length) {
    return nbt.list({ type: 'end', value: [] });
  }
  return nbt.list({ type: 'string', value: strings.map(String) });
}

/**
 * @param {string} registryVersion
 */
function createChunkAnvilEncoder(registryVersion) {
  const registry = require('prismarine-registry')(registryVersion);
  const Block = require('prismarine-block')(registry);
  const dataVersion = registry.version.dataVersion;
  const objPropsToNbt = (props) =>
    Object.fromEntries(
      Object.entries(props).map(([k, v]) => [k, nbt.string(String(v))]),
    );

  function writeSections(column) {
    const sections = [];
    const minY = column.minY >> 4;
    const maxY = column.worldHeight >> 4;

    for (let y = minY; y < maxY; y++) {
      const section = column.sections[y - minY];
      const biomeSection = column.biomes[y - minY];
      const blockLightSection = column.blockLightSections[y - minY + 1];
      const skyLightSection = column.skyLightSections[y - minY + 1];

      if (!section) continue;

      section.palette =
        section.palette === undefined ? [section.data.value] : section.palette;

      const blockPalette = section.palette.map((id) => Block.fromStateId(id)).map((block) => {
        const props = objPropsToNbt(block.getProperties());
        const entry = { Name: nbt.string(`minecraft:${block.name}`) };
        if (Object.keys(props).length) entry.Properties = nbt.comp(props);
        return entry;
      });

      const biomePalette = biomeSection.data.palette
        ? biomeSection.data.palette
        : [biomeSection.data.value];

      const biomeNamesPalette = biomePalette.map(
        (biomeId) => `minecraft:${registry.biomes[biomeId]?.name ?? 'void'}`,
      );

      const bitsPerBlock = bitsForBlockPalette(blockPalette.length);
      const bitsPerBiome = bitsForBiomePalette(biomePalette.length);

      let blockStates;
      if (!bitsPerBlock) {
        blockStates = nbt.comp({ palette: listOfCompounds(blockPalette) });
      } else {
        blockStates = nbt.comp({
          palette: listOfCompounds(blockPalette),
          data: packPalettedDataLongArray(
            section.data.data,
            bitsPerBlock,
            BLOCK_SECTION_VOLUME,
          ),
        });
      }

      let biomes;
      if (!bitsPerBiome) {
        biomes = nbt.comp({ palette: listOfStrings(biomeNamesPalette) });
      } else {
        biomes = nbt.comp({
          palette: listOfStrings(biomeNamesPalette),
          data: packPalettedDataLongArray(
            biomeSection.data.data,
            bitsPerBiome,
            BIOME_SECTION_VOLUME,
          ),
        });
      }

      let blockLight;
      let skyLight;
      if (blockLightSection) {
        blockLight =
          blockLightSection.bitsPerValue === 4
            ? new Int8Array(blockLightSection.data.buffer)
            : blockLightSection.resizeTo(4);
      }
      if (skyLightSection) {
        skyLight =
          skyLightSection.bitsPerValue === 4
            ? new Int8Array(skyLightSection.data.buffer)
            : skyLightSection.resizeTo(4);
      }

      const tag = {
        Y: nbt.byte(y),
        block_states: blockStates,
        biomes,
      };
      if (blockLight) tag.BlockLight = nbt.byteArray(blockLight);
      if (skyLight) tag.SkyLight = nbt.byteArray(skyLight);
      sections.push(tag);
    }

    return sections;
  }

  /**
   * @param {import('prismarine-chunk').Chunk} column
   * @param {number} x
   * @param {number} z
   */
  /**
   * @param {import('prismarine-chunk').Chunk} column
   * @param {number} x
   * @param {number} z
   * @param {{ referenceFormat?: boolean, skipSynthesize?: boolean, blockEntities?: object[] }} [opts]
   */
  function columnToAnvilChunkTag(column, x, z, opts = {}) {
    if (!opts.skipSynthesize) {
      synthesizeMissingBlockEntities(column, x, z);
    }

    const normalize =
      opts.referenceFormat ? normalizeBlockEntityForReference : normalizeBlockEntityForAnvil;

    const blockEntityTags =
      opts.blockEntities ??
      Object.values(column.blockEntities ?? {}).map((ent) => {
        const raw = ent?.value ?? ent;
        return normalize(raw);
      });

    const chunkTag = nbt.comp({
      DataVersion: nbt.int(dataVersion),
      Status: nbt.string('minecraft:full'),
      xPos: nbt.int(x),
      yPos: nbt.int(column.minY >> 4),
      zPos: nbt.int(z),
      block_entities: listOfCompounds(blockEntityTags),
      LastUpdate: nbt.long(column.lastUpdate ?? [Date.now() & 0xffff, 0]),
      InhabitedTime: nbt.long(column.inhabitedTime ?? 0),
      structures: nbt.comp({}),
      Heightmaps: nbt.comp({}),
      sections: listOfCompounds(writeSections(column)),
      isLightOn: nbt.byte(
        opts.referenceFormat ? 1 : column.isLightOn ? 1 : 0,
      ),
      block_ticks: nbt.list({ type: 'end', value: [] }),
      PostProcessing: nbt.list({ type: 'end', value: [] }),
      fluid_ticks: nbt.list({ type: 'end', value: [] }),
    });
    return sanitizeTagForWrite(chunkTag);
  }

  return { columnToAnvilChunkTag, dataVersion, writeSections };
}

module.exports = {
  createChunkAnvilEncoder,
  listOfCompounds,
  listOfStrings,
  packPalettedDataLongArray,
};

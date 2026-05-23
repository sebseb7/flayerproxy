'use strict';

const fs = require('fs');
const path = require('path');
const zlib = require('zlib');
const { promisify } = require('util');
const nbt = require('prismarine-nbt');
const { createLogger } = require('../utils/logger');
const { worldBoundsForDimension } = require('../state/chunkMerge');
const { mapChunkToReferenceAnvilTag } = require('./mapChunkReferenceExport');
const { writeRegionChunk } = require('./regionFile');
const { sanitizeTagForWrite } = require('./anvilBlockEntity');
const { normalizeMapChunkPacket } = require('../state/chunkMerge');

const log = createLogger('LevelSave');

const writeNbtGzip = promisify((nbtData, cb) => {
  try {
    cb(null, zlib.gzipSync(nbt.writeUncompressed(nbtData)));
  } catch (err) {
    cb(err);
  }
});

/**
 * Registry / anvil codec version for chunk load + save.
 * Must match the protocol version used during capture — state IDs differ across
 * minecraft-data releases (e.g. 1.21.10 vs 1.21.1 remap 6000+ states).
 */
function registryVersionFor(mcVersion) {
  if (require('minecraft-data')(mcVersion)) return mcVersion;
  const major = mcVersion.match(/^(1\.\d+)/)?.[1];
  if (major) {
    const versions = Object.keys(require('minecraft-data').versions.pc);
    const match = versions
      .filter((v) => v.startsWith(major))
      .sort()
      .pop();
    if (match) {
      log.warn(`No minecraft-data for ${mcVersion}; falling back to ${match}`);
      return match;
    }
  }
  return '1.21.1';
}

/** @deprecated alias */
function anvilProviderVersion(mcVersion) {
  return registryVersionFor(mcVersion);
}

/** NBT long uses two signed i32 limbs; unsigned low words fail to serialize. */
function longPair(value) {
  if (value == null) return [0, 0];
  if (Array.isArray(value)) {
    return [Number(value[0]) | 0, Number(value[1] ?? 0) | 0];
  }
  if (typeof value === 'bigint') {
    return [
      Number(value & 0xffffffffn) | 0,
      Number((value >> 32n) & 0xffffffffn) | 0,
    ];
  }
  if (typeof value === 'number') return [value | 0, 0];
  return [0, 0];
}

/**
 * Vanilla WorldGenSettings.dimensions uses keys overworld / the_nether / the_end,
 * not a nested "minecraft" compound (that yields minecraft:minecraft and fails to load).
 * @param {[number, number]} seed
 */
function buildWorldGenSettings(seed) {
  const noiseDimension = (dimensionType, noiseSettings) =>
    nbt.comp({
      type: nbt.string(dimensionType),
      generator: nbt.comp({
        type: nbt.string('minecraft:noise'),
        settings: nbt.string(noiseSettings),
        biome_source: nbt.comp({
          type: nbt.string('minecraft:multi_noise'),
          preset: nbt.string(noiseSettings),
        }),
        seed: nbt.long(seed),
      }),
    });

  return nbt.comp({
    bonus_chest: nbt.byte(0),
    generate_features: nbt.byte(1),
    seed: nbt.long(seed),
    dimensions: nbt.comp({
      overworld: noiseDimension('minecraft:overworld', 'minecraft:overworld'),
      the_nether: noiseDimension('minecraft:the_nether', 'minecraft:nether'),
      the_end: noiseDimension('minecraft:the_end', 'minecraft:end'),
    }),
  });
}

/**
 * Build level.dat NBT for a captured sniffer world (Java 1.18+ layout).
 * @param {object} meta
 */
function buildLevelDatNbt(meta) {
  const mcData = require('minecraft-data')(meta.version);
  const dataVersion = mcData.version.dataVersion;
  const seed = longPair(meta.seed);
  const spawn = meta.spawn ?? { x: 0, y: 64, z: 0 };
  const time = longPair(meta.time ?? 0);
  const dayTime = longPair(meta.dayTime ?? 6000);
  const levelName = meta.levelName ?? 'Sniffer World';

  const data = {
    DataVersion: nbt.int(dataVersion),
    LevelName: nbt.string(levelName),
    RandomSeed: nbt.long(seed),
    generatorName: nbt.string('default'),
    version: nbt.int(19133),
    Version: nbt.comp({
      Name: nbt.string(meta.minecraftVersion ?? meta.version),
      Id: nbt.int(mcData.version.version),
      Snapshot: nbt.byte(0),
      Series: nbt.string('main'),
    }),
    WorldGenSettings: buildWorldGenSettings(seed),
    SpawnX: nbt.int(spawn.x | 0),
    SpawnY: nbt.int(spawn.y | 0),
    SpawnZ: nbt.int(spawn.z | 0),
    Time: nbt.long(time),
    DayTime: nbt.long(dayTime),
    GameType: nbt.int(meta.gameType ?? 0),
    hardcore: nbt.byte(meta.hardcore ? 1 : 0),
    allowCommands: nbt.byte(1),
    MapFeatures: nbt.byte(1),
    initialized: nbt.byte(1),
    raining: nbt.byte(0),
    thundering: nbt.byte(0),
    difficulty: nbt.byte(2),
    Difficulty: nbt.byte(2),
    DifficultyLocked: nbt.byte(0),
  };

  return {
    type: 'compound',
    name: '',
    value: {
      Data: nbt.comp(data),
    },
  };
}

async function writeLevelDat(filePath, meta) {
  const payload = buildLevelDatNbt(meta);
  const data = await writeNbtGzip(payload);
  await fs.promises.writeFile(filePath, data);
}

/**
 * Write captured columns to a Java Edition world folder.
 * @param {import('./SnifferWorldCapture').SnifferWorldCapture['getExportSnapshot'] extends () => infer R ? R : never} snapshot
 * @param {string} worldDir - e.g. logs/sniffer/worlds/session-123/My World
 * @returns {Promise<{ worldDir: string, chunkCount: number, regionDir: string }|null>}
 */
async function exportSnifferWorld(snapshot, worldDir) {
  const chunks = snapshot.chunks ?? [];
  if (!chunks.length) return null;

  const regionDir = path.join(worldDir, 'region');
  await fs.promises.mkdir(regionDir, { recursive: true });

  const registryVersion = registryVersionFor(snapshot.version);
  const bounds =
    snapshot.worldBounds ??
    worldBoundsForDimension(snapshot.version, snapshot.dimensionName);

  log.info(
    `Saving ${chunks.length} chunk(s) (reference Anvil format, ${registryVersion}, capture ${snapshot.version})…`,
  );

  let saved = 0;
  let skipped = 0;
  for (const { x, z, packetData, column } of chunks) {
    try {
      const packet = normalizeMapChunkPacket(structuredClone(packetData));
      const rawTag = mapChunkToReferenceAnvilTag(
        packet,
        bounds,
        registryVersion,
        column,
      );
      const chunkTag = sanitizeTagForWrite({ type: 'compound', value: rawTag })?.value ?? rawTag;
      const regionPath = path.join(
        regionDir,
        `r.${x >> 5}.${z >> 5}.mca`,
      );
      writeRegionChunk(regionPath, x, z, chunkTag);
      saved++;
    } catch (err) {
      skipped++;
      log.warn(`Skip chunk ${x},${z}: ${err.message}`);
    }
  }

  if (!saved) {
    throw new Error(`No chunks saved (${skipped} failed)`);
  }
  if (skipped > 0) {
    log.warn(`Saved ${saved}/${chunks.length} chunks (${skipped} skipped)`);
  }

  await writeLevelDat(path.join(worldDir, 'level.dat'), {
    ...snapshot,
    minecraftVersion: snapshot.version,
    levelName: snapshot.levelName ?? path.basename(worldDir),
  });

  return { worldDir, chunkCount: saved, regionDir, skipped };
}

/**
 * @param {object} opts
 * @param {ReturnType<import('./SnifferWorldCapture').SnifferWorldCapture['getExportSnapshot']>} snapshot
 * @param {string} opts.saveDir
 * @param {string} opts.sessionId
 * @param {string} [opts.worldName]
 */
async function exportSessionWorld(opts) {
  const snapshot = opts.snapshot;
  if (!snapshot?.chunks?.length) return null;

  const safeName = (opts.worldName ?? opts.sessionId).replace(/[^\w.\- ]+/g, '_').trim() || opts.sessionId;
  const worldDir = path.join(path.resolve(opts.saveDir), safeName);
  await fs.promises.mkdir(worldDir, { recursive: true });

  snapshot.levelName = safeName;
  const result = await exportSnifferWorld(snapshot, worldDir);
  if (result) {
    log.info(`World save: ${result.worldDir} (${result.chunkCount} chunks)`);
  }
  return result;
}

module.exports = {
  anvilProviderVersion,
  registryVersionFor,
  buildWorldGenSettings,
  buildLevelDatNbt,
  writeLevelDat,
  exportSnifferWorld,
  exportSessionWorld,
};

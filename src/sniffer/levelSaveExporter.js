'use strict';

const fs = require('fs');
const zlib = require('zlib');
const { promisify } = require('util');
const nbt = require('prismarine-nbt');

const writeNbtGzip = promisify((nbtData, cb) => {
  try {
    cb(null, zlib.gzipSync(nbt.writeUncompressed(nbtData)));
  } catch (err) {
    cb(err);
  }
});

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

module.exports = { writeLevelDat };

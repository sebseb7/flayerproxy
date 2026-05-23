'use strict';

const nbt = require('prismarine-nbt');

/**
 * map_chunk blockEntities[].type → Anvil entity id for Java 1.21.10.
 * Built by pairing protocol type ids with blocks at each position in session captures
 * (not legacy wiki tables — e.g. 24 is shulker, not bed).
 * Block-at-position always wins in anvilBlockEntityId(); use this only when the block is unknown.
 */
const BLOCK_ENTITY_TYPE_BY_ID = {
  0: 'minecraft:furnace',
  1: 'minecraft:chest',
  3: 'minecraft:ender_chest',
  5: 'minecraft:dispenser',
  7: 'minecraft:sign',
  9: 'minecraft:mob_spawner',
  12: 'minecraft:brewing_stand',
  13: 'minecraft:enchanting_table',
  18: 'minecraft:hopper',
  19: 'minecraft:comparator',
  24: 'minecraft:shulker',
  25: 'minecraft:bed',
  28: 'minecraft:smoker',
  29: 'minecraft:blast_furnace',
  30: 'minecraft:lectern',
  34: 'minecraft:beehive',
};

/** Block name patterns (no namespace) → Anvil entity id. First match wins. */
const BLOCK_NAME_TO_ENTITY_ID = [
  [/wall_hanging_sign$|_hanging_sign$/, 'minecraft:hanging_sign'],
  [/wall_sign$|_sign$/, 'minecraft:sign'],
  [/_bed$/, 'minecraft:bed'],
  [/^trial_spawner$/, 'minecraft:trial_spawner'],
  [/^spawner$/, 'minecraft:mob_spawner'],
  [/_wall_banner$|_banner$/, 'minecraft:banner'],
  [/shulker_box/, 'minecraft:shulker'],
  [
    /_wall_skull$|_skull$|^skeleton_skull$|^wither_skeleton_skull$|^zombie_head$|^player_head$|^creeper_head$|^dragon_head$|^piglin_head$/,
    'minecraft:skull',
  ],
  [/^bee_nest$/, 'minecraft:beehive'],
  [/^moving_piston$/, 'minecraft:piston'],
  [/^suspicious_(sand|gravel)$/, 'minecraft:brushable_block'],
  [/^(chain_command|repeating_command|command)_block$/, 'minecraft:command_block'],
  [/^soul_campfire$|^campfire$/, 'minecraft:campfire'],
  [/^water_cauldron$|^lava_cauldron$|^powder_snow_cauldron$|^cauldron$/, 'minecraft:cauldron'],
  [/^copper_chest$|^waxed_.*_copper_chest$/, 'minecraft:chest'],
];

/** Blocks whose registry id equals the block entity type id (minecraft:{name}). */
const SAME_NAME_ENTITY_BLOCKS = new Set([
  'barrel',
  'beacon',
  'beehive',
  'bell',
  'blast_furnace',
  'brewing_stand',
  'brushable_block',
  'calibrated_sculk_sensor',
  'campfire',
  'chest',
  'chiseled_bookshelf',
  'command_block',
  'comparator',
  'conduit',
  'crafter',
  'creaking_heart',
  'daylight_detector',
  'decorated_pot',
  'dispenser',
  'dropper',
  'enchanting_table',
  'end_gateway',
  'end_portal',
  'ender_chest',
  'furnace',
  'hopper',
  'jigsaw',
  'jukebox',
  'lectern',
  'mob_spawner',
  'piston',
  'sculk_catalyst',
  'sculk_sensor',
  'sculk_shrieker',
  'shulker_box',
  'smoker',
  'structure_block',
  'trapped_chest',
  'trial_spawner',
  'vault',
]);

/** Stored/wrong ids → vanilla registry id. */
const ENTITY_ID_ALIASES = {
  'minecraft:spawner': 'minecraft:mob_spawner',
};

const SIGN_ENTITY_ID = 'minecraft:sign';

const EMPTY_STRING_LIST = () =>
  nbt.list({ type: 'string', value: ['', '', '', ''] });

/** @param {import('prismarine-nbt').Tag|string|undefined} tag */
function nbtStringValue(tag) {
  if (tag == null) return '';
  if (typeof tag === 'string') return tag;
  return tag.value ?? '';
}

/**
 * @param {string} [blockName]
 * @param {number} [protocolTypeId]
 * @returns {string|null}
 */
function anvilBlockEntityId(blockName, protocolTypeId) {
  const name = blockName?.replace(/^minecraft:/, '') ?? '';
  for (const [re, id] of BLOCK_NAME_TO_ENTITY_ID) {
    if (re.test(name)) return id;
  }
  if (name && SAME_NAME_ENTITY_BLOCKS.has(name)) {
    return `minecraft:${name}`;
  }
  if (protocolTypeId != null && BLOCK_ENTITY_TYPE_BY_ID[protocolTypeId]) {
    return BLOCK_ENTITY_TYPE_BY_ID[protocolTypeId];
  }
  return null;
}

/**
 * @param {string} blockName
 * @returns {boolean}
 */
function blockRequiresBlockEntity(blockName) {
  return anvilBlockEntityId(blockName) != null;
}

/**
 * @param {string} entityId
 * @returns {Record<string, object>}
 */
/** Vanilla 1.21 chunk block entities include these on every entry (see logs/sniffer/worlds/test). */
function ensureVanillaBlockEntityShell(value) {
  if (value.keepPacked == null) value.keepPacked = nbt.byte(0);
  if (value.components == null) value.components = nbt.comp({});
  return value;
}

function stubBlockEntityValue(entityId) {
  const id = entityId.replace(/^minecraft:/, '');
  const value = ensureVanillaBlockEntityShell({});

  if (/chest|barrel|shulker|furnace|hopper|dispenser|dropper|brewing|smoker|blast_furnace|crafter/i.test(id)) {
    value.Items = nbt.list({ type: 'compound', value: [] });
  }
  if (id === 'sign' || id === 'hanging_sign') {
    const textSide = nbt.comp({
      has_glowing_text: nbt.byte(0),
      color: nbt.string('black'),
      messages: EMPTY_STRING_LIST(),
    });
    value.front_text = textSide;
    value.back_text = textSide;
    value.is_waxed = nbt.byte(0);
    value.keepPacked = nbt.byte(0);
    value.components = nbt.comp({});
  }
  if (id === 'bed') {
    value.color = nbt.string('white');
  }
  if (id === 'mob_spawner') {
    value.SpawnData = nbt.comp({
      entity: nbt.comp({ id: nbt.string('minecraft:pig') }),
    });
    value.SpawnPotentials = nbt.list({ type: 'compound', value: [] });
    value.Delay = nbt.short(20);
    value.MinSpawnDelay = nbt.short(200);
    value.MaxSpawnDelay = nbt.short(800);
  }
  if (id === 'trial_spawner') {
    value.shared_data = nbt.comp({});
  }
  if (id === 'vault') {
    value.shared_data = nbt.comp({});
  }
  if (id === 'decorated_pot') {
    value.sherds = nbt.list({ type: 'string', value: [] });
  }
  if (id === 'beehive') {
    value.Bees = nbt.list({ type: 'compound', value: [] });
  }
  if (id === 'banner') {
    value.CustomName = nbt.string('');
    value.patterns = nbt.list({ type: 'compound', value: [] });
  }
  if (id === 'skull') {
    value.SkullOwner = nbt.comp({});
  }
  if (id === 'command_block') {
    value.Command = nbt.string('');
    value.SuccessCount = nbt.int(0);
  }
  if (id === 'jukebox') {
    value.RecordItem = nbt.comp({});
  }
  if (id === 'lectern') {
    value.Book = nbt.comp({});
  }
  if (id === 'brushable_block') {
    value.LootTable = nbt.string('');
    value.LootTableSeed = nbt.long([0, 0]);
  }

  return value;
}

/**
 * @param {string} id
 * @returns {string}
 */
function canonicalizeEntityId(id) {
  if (!id) return id;
  let out = ENTITY_ID_ALIASES[id] ?? id;
  if (/^minecraft:[a-z]+_bed$/.test(out)) out = 'minecraft:bed';
  if (/^minecraft:.+_sign$/.test(out) || /^minecraft:.+_hanging_sign$/.test(out)) {
    out = /hanging/.test(out) ? 'minecraft:hanging_sign' : 'minecraft:sign';
  }
  if (/^minecraft:.+_banner$/.test(out) || /^minecraft:.+_wall_banner$/.test(out)) {
    out = 'minecraft:banner';
  }
  if (out === 'minecraft:shulker_box' || /^minecraft:.+_shulker_box$/.test(out)) {
    out = 'minecraft:shulker';
  }
  return out;
}

/**
 * Reference save (session-1779506450054-2): protocol type → Anvil id on disk.
 * @param {number} [protocolTypeId]
 * @returns {string|null}
 */
function referenceEntityIdFromType(protocolTypeId) {
  if (protocolTypeId === 24) return 'minecraft:shulker_box';
  const id = BLOCK_ENTITY_TYPE_BY_ID[protocolTypeId];
  if (!id) return null;
  if (id === 'minecraft:shulker') return 'minecraft:shulker_box';
  return id;
}

/**
 * Anvil block entity tag matching reference world format (-2).
 * @param {object} entity
 * @param {string} [entityIdHint]
 */
function normalizeBlockEntityForReference(entity, entityIdHint) {
  const raw = entity?.type === 'compound' && entity.value != null ? entity.value : entity;
  if (!raw || typeof raw !== 'object') return raw;

  const out = { ...raw };
  let id = nbtStringValue(out.id) || entityIdHint || '';
  if (id === 'minecraft:shulker') id = 'minecraft:shulker_box';
  if (id) out.id = nbt.string(id);

  ensureVanillaBlockEntityShell(out);

  if (id === 'minecraft:shulker_box' && out.Items != null) {
    const items = out.Items?.value?.value ?? out.Items?.value;
    if (!items?.length) delete out.Items;
  }

  const sanitized = sanitizeTagForWrite({ type: 'compound', value: out });
  return sanitized?.value ?? out;
}

/**
 * prismarine-nbt drops bool(false) on write — convert to byte for Anvil compatibility.
 * @param {import('prismarine-nbt').Tag|object|null|undefined} tag
 */
function sanitizeTagForWrite(tag) {
  if (tag == null) return tag;
  if (tag.type === 'bool') {
    return nbt.byte(tag.value ? 1 : 0);
  }
  if (tag.type === 'compound' && tag.value != null) {
    const value = {};
    for (const [k, v] of Object.entries(tag.value)) {
      value[k] = sanitizeTagForWrite(v);
    }
    return nbt.comp(value);
  }
  if (tag.type === 'list') {
    const listType = tag.value?.type ?? 'end';
    const items = tag.value?.value ?? [];
    if (!items.length) {
      return nbt.list({ type: 'end', value: [] });
    }
    return nbt.list({
      type: listType,
      value: items.map((item) => sanitizeTagForWrite(item)),
    });
  }
  return tag;
}

/**
 * @param {object} entity - column blockEntities entry (compound tag or plain value map)
 */
function normalizeBlockEntityForAnvil(entity) {
  const raw = entity?.type === 'compound' && entity.value != null ? entity.value : entity;
  if (!raw || typeof raw !== 'object') return raw;

  const out = { ...raw };
  let id = canonicalizeEntityId(nbtStringValue(out.id));
  if (id) {
    out.id = nbt.string(id);
  }

  const hasSignText = out.front_text != null && out.back_text != null;
  if (/hanging_sign/i.test(id)) {
    out.id = nbt.string('minecraft:hanging_sign');
    if (!out.keepPacked) out.keepPacked = nbt.byte(0);
    if (!out.components) out.components = nbt.comp({});
  } else if (/sign/i.test(id) || hasSignText) {
    out.id = nbt.string(SIGN_ENTITY_ID);
  }

  ensureVanillaBlockEntityShell(out);

  const sanitized = sanitizeTagForWrite({ type: 'compound', value: out });
  return sanitized?.value ?? out;
}

/**
 * map_chunk often omits block_entities for blocks that still need them in Anvil.
 * @param {import('prismarine-chunk').Chunk} column
 * @param {number} chunkX
 * @param {number} chunkZ
 */
function synthesizeMissingBlockEntities(column, chunkX, chunkZ) {
  const occupied = new Set();
  for (const ent of Object.values(column.blockEntities ?? {})) {
    const v = ent?.value ?? ent;
    const wx = v.x?.value ?? v.x;
    const wy = v.y?.value ?? v.y;
    const wz = v.z?.value ?? v.z;
    if (wx != null) occupied.add(`${wx},${wy},${wz}`);
  }

  const minY = column.minY;
  const maxY = column.minY + column.worldHeight;

  for (let y = minY; y < maxY; y++) {
    for (let lx = 0; lx < 16; lx++) {
      for (let lz = 0; lz < 16; lz++) {
        const block = column.getBlock({ x: lx, y, z: lz });
        if (!block?.name || block.name === 'air') continue;

        const entityId = anvilBlockEntityId(block.name);
        if (!entityId) continue;

        const wx = chunkX * 16 + lx;
        const wz = chunkZ * 16 + lz;
        const key = `${wx},${y},${wz}`;
        if (occupied.has(key)) continue;

        const value = stubBlockEntityValue(entityId);
        value.x = nbt.int(wx);
        value.y = nbt.int(y);
        value.z = nbt.int(wz);
        value.id = nbt.string(entityId);
        column.setBlockEntity(
          { x: lx, y, z: lz },
          { type: 'compound', name: '', value },
        );
        occupied.add(key);
      }
    }
  }
}

module.exports = {
  BLOCK_ENTITY_TYPE_BY_ID,
  ensureVanillaBlockEntityShell,
  anvilBlockEntityId,
  blockRequiresBlockEntity,
  stubBlockEntityValue,
  normalizeBlockEntityForAnvil,
  normalizeBlockEntityForReference,
  referenceEntityIdFromType,
  sanitizeTagForWrite,
  synthesizeMissingBlockEntities,
  nbtStringValue,
};

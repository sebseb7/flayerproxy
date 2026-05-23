'use strict';

const nbt = require('prismarine-nbt');
const UUID = require('uuid-1345');
const { sanitizeTagForWrite } = require('./anvilBlockEntity');
const { listOfCompounds } = require('./chunkAnvilEncode');
const {
  villagerTypeName,
  villagerProfessionName,
} = require('./entityVillagerRegistries');

/**
 * Vanilla entity column layout (vanillaEntityExample/session-* entities/*.mca).
 * Root: Position [chunkX, chunkZ], DataVersion, Entities.
 * Per-entity: Pos/Motion/Rotation lists, UUID int[4], riders in Passengers on vehicle.
 */

/** @type {Map<string, ReturnType<typeof import('prismarine-item')>>} */
const itemRegistryCache = new Map();

function itemModule(version) {
  if (!itemRegistryCache.has(version)) {
    itemRegistryCache.set(version, require('prismarine-item')(version));
  }
  return itemRegistryCache.get(version);
}

function vec3DoubleTag(x, y, z) {
  return nbt.list({ type: 'double', value: [x ?? 0, y ?? 0, z ?? 0] });
}

function normalizeDegrees(deg) {
  return ((deg % 360) + 360) % 360;
}

function rotationTag(yaw, pitch) {
  return nbt.list({
    type: 'float',
    value: [normalizeDegrees(yaw ?? 0), normalizeDegrees(pitch ?? 0)],
  });
}

/** Motion for Anvil from tracked velocity (sync_entity_position / entity_velocity). */
function motionComponents(spawn) {
  if (spawn.velocityKnown && spawn.velocity) {
    const { x = 0, y = 0, z = 0 } = spawn.velocity;
    return [x, y, z];
  }
  return [0, 0, 0];
}

function protocolUuidToBuffer(uuid) {
  if (Buffer.isBuffer(uuid)) return uuid.length === 16 ? uuid : null;
  if (typeof uuid === 'string') {
    try {
      return UUID.parse(uuid);
    } catch (_) {
      const hex = uuid.replace(/-/g, '');
      if (hex.length === 32) return Buffer.from(hex, 'hex');
    }
  }
  if (Array.isArray(uuid) && uuid.length === 4 && typeof uuid[0] === 'number') {
    const buf = Buffer.alloc(16);
    for (let i = 0; i < 4; i++) buf.writeInt32BE(uuid[i], i * 4);
    return buf;
  }
  return null;
}

/** i8 spawn yaw/pitch → vanilla NBT Rotation (degrees). */
function protocolByteToDegrees(b) {
  if (typeof b !== 'number') return 0;
  const unsigned = b < 0 ? b + 256 : b;
  return (unsigned * 360) / 256;
}

function spawnRotationDegrees(spawn) {
  const yaw = spawn.yaw ?? 0;
  const pitch = spawn.pitch ?? 0;
  if (Math.abs(yaw) > 128 && Math.abs(yaw) <= 360) {
    return [((yaw % 360) + 360) % 360, protocolByteToDegrees(pitch)];
  }
  return [protocolByteToDegrees(yaw), protocolByteToDegrees(pitch)];
}

function uuidIntArrayTag(uuid) {
  const buf = protocolUuidToBuffer(uuid);
  if (!buf) return null;
  return nbt.intArray([
    buf.readInt32BE(0),
    buf.readInt32BE(4),
    buf.readInt32BE(8),
    buf.readInt32BE(12),
  ]);
}


function entityIdFromType(version, typeId) {
  const mcData = require('minecraft-data')(version);
  const ent = mcData.entitiesArray?.[typeId];
  if (!ent?.name) return null;
  return ent.name.includes(':') ? ent.name : `minecraft:${ent.name}`;
}

function isBoat(id) {
  return id.includes('_boat');
}

function isMinecart(id) {
  return id.includes('minecart');
}

function isPassengerCarrier(id) {
  return isBoat(id) || isMinecart(id);
}

function isItemFrame(id) {
  return id === 'minecraft:item_frame' || id === 'minecraft:glow_item_frame';
}

function isProjectile(id) {
  return (
    id.includes('pearl') ||
    id.includes('arrow') ||
    id.includes('snowball') ||
    id.includes('egg') ||
    id.includes('bottle') ||
    id.includes('fireball') ||
    (id.includes('trident') && !id.includes('item'))
  );
}

function isLivingMob(id) {
  if (isBoat(id) || isItemFrame(id) || isProjectile(id) || isMinecart(id)) return false;
  if (id === 'minecraft:item' || id === 'minecraft:experience_orb') return false;
  return true;
}

function metadataKeyIndex(version, entityId, name) {
  const short = entityId.replace('minecraft:', '');
  const keys = require('minecraft-data')(version).entitiesByName[short]?.metadataKeys;
  if (!keys) return -1;
  return keys.indexOf(name);
}

function metaEntry(metadata, key) {
  if (!metadata?.metadata?.length) return null;
  return metadata.metadata.find((e) => e.key === key) ?? null;
}

function yawToFacing(deg) {
  const n = ((deg % 360) + 360) % 360;
  if (n >= 315 || n < 45) return 3;
  if (n < 135) return 5;
  if (n < 225) return 2;
  return 4;
}

/** spawn_entity.objectData / metadata.direction → vanilla Facing byte (Direction.get3DDataValue). */
function itemFrameFacingByte(spawn, tracked, version, entityId) {
  const dirKey = metadataKeyIndex(version, entityId, 'direction');
  const dirEntry = dirKey >= 0 ? metaEntry(tracked.metadata, dirKey) : null;
  const fromMeta = dirEntry?.value;
  if (typeof fromMeta === 'number' && fromMeta >= 0 && fromMeta <= 5) return fromMeta;
  if (typeof spawn.objectData === 'number' && spawn.objectData >= 0 && spawn.objectData <= 5) {
    return spawn.objectData;
  }
  return yawToFacing(spawnRotationDegrees(spawn)[0]);
}

function simpleItemStack(protocolItem, version) {
  if (!protocolItem) return null;
  const stack = itemStackTag(protocolItem, version);
  if (!stack) return null;
  const v = stack.value ?? stack;
  if (v.id && v.count) return nbt.comp({ id: v.id, count: v.count });
  return stack;
}

function itemStackTag(protocolItem, version) {
  if (!protocolItem) return null;
  try {
    const Item = itemModule(version);
    const item = Item.fromNotch(protocolItem, Item.anvilPacket);
    if (!item || item.blockId < 0) return null;
    const tag = item.nbtData;
    if (tag?.type === 'compound') return tag;
    const comp = {
      id: nbt.string(item.name.includes(':') ? item.name : `minecraft:${item.name}`),
      count: nbt.byte(item.count ?? 1),
    };
    if (tag) comp.tag = tag;
    return nbt.comp(comp);
  } catch (_) {
    if (protocolItem.itemId != null) {
      const mcData = require('minecraft-data')(version);
      const name = mcData.items?.[protocolItem.itemId]?.name;
      if (name) {
        return nbt.comp({
          id: nbt.string(name.includes(':') ? name : `minecraft:${name}`),
          count: nbt.byte(protocolItem.itemCount ?? 1),
        });
      }
    }
    return null;
  }
}

/** Universal fields present on vanilla examples (boat, frame, chicken). */
function baseEntityTags(spawn, id) {
  const [yaw, pitch] = spawnRotationDegrees(spawn);
  const [mx, my, mz] = motionComponents(spawn);
  return {
    id: nbt.string(id),
    UUID: uuidIntArrayTag(spawn.objectUUID ?? spawn.uuid),
    Pos: vec3DoubleTag(spawn.x, spawn.y, spawn.z),
    Motion: vec3DoubleTag(mx, my, mz),
    Rotation: rotationTag(yaw, pitch),
    Air: nbt.short(300),
    Fire: nbt.short(0),
    OnGround: nbt.byte(spawn.onGround === false ? 0 : 1),
    fall_distance: nbt.double(0),
    Invulnerable: nbt.byte(0),
    PortalCooldown: nbt.int(0),
  };
}

function applyItemFrameTags(value, tracked, version) {
  const spawn = tracked.spawnData;
  const md = tracked.metadata;
  const itemKey = metadataKeyIndex(version, value.id.value, 'item');
  const rotKey = metadataKeyIndex(version, value.id.value, 'rotation');

  const facing = itemFrameFacingByte(spawn, tracked, version, value.id.value);

  value.Facing = nbt.byte(facing);
  value.block_pos = nbt.intArray([
    Math.floor(spawn.x),
    Math.floor(spawn.y),
    Math.floor(spawn.z),
  ]);
  value.ItemRotation = nbt.byte(0);
  value.ItemDropChance = nbt.float(1);
  value.Invisible = nbt.byte(0);
  value.Fixed = nbt.byte(0);

  const rotEntry = rotKey >= 0 ? metaEntry(md, rotKey) : null;
  if (rotEntry?.value != null) value.ItemRotation = nbt.byte(rotEntry.value);

  const itemEntry = itemKey >= 0 ? metaEntry(md, itemKey) : null;
  if (itemEntry?.value) {
    const stack = simpleItemStack(itemEntry.value, version);
    if (stack) value.Item = stack;
  }

  const flagsEntry = metaEntry(md, 0);
  if (flagsEntry?.value != null && typeof flagsEntry.value === 'number') {
    const invisible = (flagsEntry.value & 0x20) !== 0;
    value.Invisible = nbt.byte(invisible ? 1 : 0);
  }
}

function applyLivingMobTags(value, tracked, version, id) {
  const healthKey = metadataKeyIndex(version, id, 'health');
  const healthEntry = healthKey >= 0 ? metaEntry(tracked.metadata, healthKey) : null;
  if (healthEntry?.type === 'float') {
    value.Health = nbt.float(healthEntry.value);
  } else if (id === 'minecraft:chicken') {
    value.Health = nbt.float(4);
  } else {
    value.Health = nbt.float(20);
  }

  value.HurtTime = nbt.short(0);
  value.DeathTime = nbt.short(0);
  value.HurtByTimestamp = nbt.int(0);
  value.AbsorptionAmount = nbt.float(0);
  value.FallFlying = nbt.byte(0);
  value.LeftHanded = nbt.byte(0);
  value.CanPickUpLoot = nbt.byte(0);
  value.PersistenceRequired = nbt.byte(0);

  if (id === 'minecraft:chicken') {
    value.Brain = nbt.comp({ memories: nbt.comp({}) });
    value.variant = nbt.string('minecraft:warm');
    value.Age = nbt.int(0);
    value.ForcedAge = nbt.int(0);
    value.InLove = nbt.byte(0);
    value.IsChickenJockey = nbt.byte(0);
    value.EggLayTime = nbt.int(0);
  }

  if (id === 'minecraft:villager' || id === 'minecraft:zombie_villager') {
    applyVillagerTags(value, tracked, version);
  }

  applyEquipmentTags(value, tracked, version);
}

function applyVillagerTags(value, tracked, version) {
  const vdKey = metadataKeyIndex(version, 'minecraft:villager', 'villager_data');
  const vdEntry = vdKey >= 0 ? metaEntry(tracked.metadata, vdKey) : null;
  const vd = vdEntry?.value;
  if (!vd) return;

  value.VillagerData = nbt.comp({
    type: nbt.string(villagerTypeName(vd.villagerType)),
    profession: nbt.string(villagerProfessionName(vd.villagerProfession)),
    level: nbt.int(vd.level ?? 1),
  });
  value.Xp = nbt.int(0);
  if (value.Age == null) value.Age = nbt.int(0);
  if (value.ForcedAge == null) value.ForcedAge = nbt.int(0);
  value.FoodLevel = nbt.int(0);
  value.Gossips = listOfCompounds([]);
  value.Inventory = listOfCompounds([]);
  if (!value.Brain) {
    value.Brain = nbt.comp({ memories: nbt.comp({}) });
  }
}

/** Protocol equipment slot index → vanilla 1.21 NBT equipment key. */
const EQUIPMENT_SLOT_NAMES = {
  0: 'mainhand',
  1: 'offhand',
  2: 'feet',
  3: 'legs',
  4: 'chest',
  5: 'head',
  6: 'body',
  7: 'saddle',
};

function equipmentStackTag(protocolItem, version) {
  if (!protocolItem || protocolItem.itemId < 0 || protocolItem.itemCount <= 0) {
    return null;
  }
  const stack = simpleItemStack(protocolItem, version);
  if (!stack) return null;
  const inner = stack.value ?? stack;
  const idTag = inner.id;
  const countTag = inner.count;
  if (!idTag || !countTag) return null;
  return nbt.comp({
    id: idTag,
    count: countTag,
  });
}

/**
 * Vanilla 1.21+ entity equipment: equipment.mainhand + drop_chances.mainhand (not HandItems).
 */
function applyEquipmentTags(value, tracked, version) {
  const equipments = tracked.equipment?.equipments;
  if (!equipments?.length) return;

  const equipmentComp = {};
  const dropChances = {};

  for (const { slot, item } of equipments) {
    const name = EQUIPMENT_SLOT_NAMES[slot];
    if (!name) continue;
    const stackTag = equipmentStackTag(item, version);
    if (!stackTag) continue;
    equipmentComp[name] = stackTag;
    dropChances[name] = nbt.float(2);
  }

  if (Object.keys(equipmentComp).length) {
    value.equipment = nbt.comp(equipmentComp);
    value.drop_chances = nbt.comp(dropChances);
  }
}

/** Husk/zombie fields from vanilla entity columns. */
function applyZombieFamilyTags(value, id) {
  if (id !== 'minecraft:husk' && id !== 'minecraft:zombie' && id !== 'minecraft:zombie_villager') {
    return;
  }
  if (!value.Brain) {
    value.Brain = nbt.comp({ memories: nbt.comp({}) });
  }
  value.CanBreakDoors = nbt.byte(0);
  value.DrownedConversionTime = nbt.int(-1);
  value.IsBaby = nbt.byte(0);
  value.InWaterTime = nbt.int(-1);
  value.CanPickUpLoot = nbt.byte(id === 'minecraft:husk' ? 1 : 0);
  value.PersistenceRequired = nbt.byte(id === 'minecraft:husk' ? 1 : 0);
}

function attachPassengerTags(value, tracked, version, lookupEntity, idStr) {
  if (!isPassengerCarrier(idStr) || !tracked.passengers?.passengers?.length) return;

  const passengerTags = [];
  for (const pid of tracked.passengers.passengers) {
    const row = lookupEntity(pid);
    if (!row) continue;
    const tag = entityNbtFromTracked(row, version, lookupEntity, { asPassenger: true });
    if (tag) passengerTags.push(tag);
  }
  if (passengerTags.length) {
    value.Passengers = listOfCompounds(passengerTags);
  }
}

function applyProjectileItem(value, tracked, version) {
  const itemKey = metadataKeyIndex(version, value.id.value, 'item_stack');
  const itemEntry = itemKey >= 0 ? metaEntry(tracked.metadata, itemKey) : null;
  if (itemEntry?.value) {
    const stack = simpleItemStack(itemEntry.value, version);
    if (stack) value.Item = stack;
  }
}

/**
 * @param {object} tracked
 * @param {string} version
 * @param {(id: number) => object|null} lookupEntity
 * @param {{ asPassenger?: boolean }} [ctx]
 */
function entityNbtFromTracked(tracked, version, lookupEntity, ctx = {}) {
  const spawn = tracked.spawnData;
  if (!spawn) return null;

  const id = entityIdFromType(version, spawn.type);
  if (!id) return null;
  if (!uuidIntArrayTag(spawn.objectUUID ?? spawn.uuid)) return null;

  const value = baseEntityTags(spawn, id);
  const idStr = id;

  if (isItemFrame(idStr)) {
    applyItemFrameTags(value, tracked, version);
  } else if (isLivingMob(idStr)) {
    applyLivingMobTags(value, tracked, version, idStr);
    applyZombieFamilyTags(value, idStr);
  } else if (isProjectile(idStr)) {
    applyProjectileItem(value, tracked, version);
  }

  if (!ctx.asPassenger) {
    attachPassengerTags(value, tracked, version, lookupEntity, idStr);
  }

  const sanitized = sanitizeTagForWrite({ type: 'compound', value });
  return sanitized?.value ?? value;
}

/**
 * @param {number} chunkX
 * @param {number} chunkZ
 * @param {object[]} trackedEntities - top-level only (passengers excluded)
 * @param {{ version: string, lookupEntity?: (id: number) => object|null }} opts
 */
function entityChunkTagFromEntities(chunkX, chunkZ, trackedEntities, opts) {
  const mcData = require('minecraft-data')(opts.version);
  const lookup = opts.lookupEntity ?? (() => null);
  const entityTags = [];

  for (const tracked of trackedEntities) {
    const tag = entityNbtFromTracked(tracked, opts.version, lookup);
    if (tag) entityTags.push(tag);
  }

  const chunkTag = {
    Position: nbt.intArray([chunkX, chunkZ]),
    DataVersion: nbt.int(mcData.version.dataVersion),
    Entities: listOfCompounds(entityTags),
  };

  return sanitizeTagForWrite({ type: 'compound', value: chunkTag })?.value ?? chunkTag;
}

function emptyEntityChunkTag(chunkX, chunkZ, opts) {
  return entityChunkTagFromEntities(chunkX, chunkZ, [], opts);
}

/**
 * Build export rows for a chunk: omit entities listed as another entity's passenger.
 * @param {object[]} rows - EntityRegionCache export rows
 */
function topLevelEntitiesForChunk(rows) {
  const passengerIds = new Set();
  for (const row of rows) {
    for (const pid of row.passengers?.passengers ?? []) {
      passengerIds.add(pid);
    }
  }
  return rows.filter((row) => !passengerIds.has(row.entityId));
}

module.exports = {
  entityChunkTagFromEntities,
  emptyEntityChunkTag,
  entityNbtFromTracked,
  entityIdFromType,
  protocolUuidToBuffer,
  topLevelEntitiesForChunk,
  itemFrameFacingByte,
};

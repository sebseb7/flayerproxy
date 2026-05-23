'use strict';

const mc = require('minecraft-protocol');
const { createLogger } = require('../utils/logger');
const { parseSpawnEntityVelocity } = require('../network/spawnEntityWire');
const { read: readLpVec3 } = require('../network/LpVec3');

const motionLog = createLogger('MotionPkt');
const states = mc.states;

/** @type {Map<string, ReturnType<typeof import('minecraft-data')>>} */
const mcDataCache = new Map();

const MOTION_PACKET_NAMES = new Set([
  'spawn_entity',
  'entity_velocity',
  'sync_entity_position',
  'rel_entity_move',
  'entity_move_look',
  'set_projectile_power',
]);

const PROJECTILE_NAME_HINTS = [
  'pearl',
  'arrow',
  'snowball',
  'egg',
  'bottle',
  'potion',
  'fireball',
  'wind_charge',
  'trident',
  'firework',
  'bobber',
  'wither_skull',
  'shulker_bullet',
  'llama_spit',
  'dragon_fireball',
  'trident',
  'spectral_arrow',
];

function mcDataForVersion(version) {
  if (!version) return null;
  if (!mcDataCache.has(version)) {
    mcDataCache.set(version, require('minecraft-data')(version));
  }
  return mcDataCache.get(version);
}

function entityTypeLabel(version, typeId) {
  if (typeId == null) return null;
  const ent = mcDataForVersion(version)?.entitiesArray?.[typeId];
  if (!ent?.name) return String(typeId);
  return ent.name.includes(':') ? ent.name : `minecraft:${ent.name}`;
}

function isProjectileTypeName(typeName) {
  if (!typeName) return false;
  const base = typeName.replace('minecraft:', '');
  return PROJECTILE_NAME_HINTS.some((hint) => base.includes(hint));
}

function deltaBlocks(d) {
  if (d == null) return null;
  return d / 4096;
}

function vec3BlocksPerTick(x, y, z) {
  return { x, y, z };
}

function summarizeMotionPacket(name, data, buffer, version) {
  const base = { packet: name };

  switch (name) {
    case 'spawn_entity': {
      const typeName = entityTypeLabel(version, data?.type);
      base.entityId = data?.entityId;
      base.type = data?.type;
      base.typeName = typeName;
      base.projectile = isProjectileTypeName(typeName);
      if (data?.x != null) {
        base.pos = { x: data.x, y: data.y, z: data.z };
      }
      base.velocityProtodef = data?.velocity ?? null;
      if (buffer?.length) {
        base.rawBytes = buffer.length;
        base.rawHex = buffer.toString('hex');
        try {
          const wire = parseSpawnEntityVelocity(buffer);
          base.velocityHex = wire.velocityHex;
          base.velocityLpVec3 = wire.velocity;
          base.velocityLpSize = wire.lpSize;
        } catch (err) {
          base.velocityParseError = err.message;
        }
      }
      return base;
    }
    case 'entity_velocity': {
      base.entityId = data?.entityId;
      base.velocityProtodef = data?.velocity ?? null;
      if (buffer?.length) {
        base.rawBytes = buffer.length;
        try {
          const lp = readEntityVelocityFromWire(buffer);
          base.velocityHex = buffer.subarray(lp.offset, lp.offset + lp.size).toString('hex');
          base.velocityLpVec3 = lp.value;
          base.velocityLpSize = lp.size;
        } catch (err) {
          base.velocityParseError = err.message;
        }
      }
      return base;
    }
    case 'sync_entity_position': {
      base.entityId = data?.entityId;
      base.pos =
        data?.x != null ? { x: data.x, y: data.y, z: data.z } : null;
      base.velocity = vec3BlocksPerTick(data?.dx, data?.dy, data?.dz);
      return base;
    }
    case 'rel_entity_move': {
      base.entityId = data?.entityId;
      base.delta = {
        dX: data?.dX,
        dY: data?.dY,
        dZ: data?.dZ,
      };
      base.deltaBlocks = {
        x: deltaBlocks(data?.dX),
        y: deltaBlocks(data?.dY),
        z: deltaBlocks(data?.dZ),
      };
      base.onGround = data?.onGround;
      return base;
    }
    case 'entity_move_look': {
      base.entityId = data?.entityId;
      base.delta = {
        dX: data?.dX,
        dY: data?.dY,
        dZ: data?.dZ,
      };
      base.deltaBlocks = {
        x: deltaBlocks(data?.dX),
        y: deltaBlocks(data?.dY),
        z: deltaBlocks(data?.dZ),
      };
      base.yaw = data?.yaw;
      base.pitch = data?.pitch;
      base.onGround = data?.onGround;
      return base;
    }
    case 'set_projectile_power': {
      base.entityId = data?.id;
      base.accelerationPower = data?.accelerationPower;
      base.projectile = true;
      return base;
    }
    default:
      return base;
  }
}

function readVarInt(buffer, offset = 0) {
  let out = 0;
  let bytes = 0;
  let pos = offset;
  while (true) {
    const in_ = buffer[pos];
    out |= (in_ & 127) << (bytes++ * 7);
    if (bytes > 5) throw new Error('VarInt too big');
    pos += 1;
    if ((in_ & 128) !== 128) break;
  }
  return { value: out, size: pos - offset };
}

/** Play packet buffer: packetId + entityId + lpVec3. */
function readEntityVelocityFromWire(buffer) {
  let off = 0;
  off += readVarInt(buffer, off).size;
  off += readVarInt(buffer, off).size;
  const velOff = off;
  const lp = readLpVec3(buffer, off);
  return { ...lp, offset: velOff };
}

/**
 * Console line for spawn / velocity / move packets (including relayed S2C).
 * @param {object} opts
 * @param {string} [opts.version]
 * @param {object} meta
 * @param {object} data
 * @param {Buffer} [buffer]
 * @param {object} [extra]
 */
function logMotionPacketConsole(opts, meta, data, buffer, extra = {}) {
  if (!meta?.name || !MOTION_PACKET_NAMES.has(meta.name)) return;
  if (meta.state !== states.PLAY) return;
  if (extra.dir && extra.dir !== 'S2C') return;
  if (extra.leg && extra.leg !== 'backend') return;

  const entry = {
    ...summarizeMotionPacket(meta.name, data, buffer, opts?.version),
    action: extra.action ?? extra.forwarded ?? null,
    leg: extra.leg,
    dir: extra.dir ?? 'S2C',
  };

  motionLog.info(entry);
}

module.exports = {
  MOTION_PACKET_NAMES,
  logMotionPacketConsole,
  summarizeMotionPacket,
  isProjectileTypeName,
};

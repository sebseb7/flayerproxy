import chalk from 'chalk';
import { createRequire } from 'node:module';
import { formatEntityType } from './entityTypeNames.js';

const require = createRequire(import.meta.url);
const lc = require('../libchunk/js/index.js');

/** @typedef {{ id: number, type: number, uuid: string, x: number, y: number, z: number, posKnown: boolean, vel: { x: number, y: number, z: number }, rot: { pitch: number, yaw: number, headPitch: number, headYaw?: number }, data: number, spawnTime: number, metadata?: Record<number, any>, equipment?: Record<number, any>, attributes?: Record<number, any>, effects?: Record<number, any>, passengers?: number[], attachedTo?: number, status?: number }} TrackedEntity */

/** @param {number} x @param {number} y @param {number} z */
function isValidCoord(x, y, z) {
  return (
    Number.isFinite(x) &&
    Number.isFinite(y) &&
    Number.isFinite(z) &&
    Math.abs(x) < 3e7 &&
    Math.abs(y) < 3e7 &&
    Math.abs(z) < 3e7
  );
}

export function createEntityTracker() {
  /** @type {Map<number, TrackedEntity>} */
  const entities = new Map();

  function get(id) {
    return entities.get(id);
  }

  function set(id, ent) {
    entities.set(id, ent);
  }

  function remove(id) {
    entities.delete(id);
  }

  /** @param {TrackedEntity} e */
  function updatePos(e, x, y, z) {
    if (!isValidCoord(x, y, z)) return;
    e.x = x;
    e.y = y;
    e.z = z;
    e.posKnown = true;
  }

  function isFinitePos(e) {
    return e.posKnown && isValidCoord(e.x, e.y, e.z);
  }

  /** @param {TrackedEntity} e */
  function formatEntity(e) {
    const label = formatEntityType(e.type);
    if (!isFinitePos(e)) return label;
    return `${label} pos=(${e.x.toFixed(3)},${e.y.toFixed(3)},${e.z.toFixed(3)})`;
  }

  /** @param {number[]} ids @returns {string | null} */
  function suffixForIds(ids) {
    const parts = [];
    for (const id of ids) {
      const e = get(id);
      if (!e) continue;
      parts.push(formatEntity(e));
    }
    if (parts.length === 0) return null;
    return chalk.dim(` entity[${parts.join('; ')}]`);
  }

  function handleSpawn(payload) {
    const p = lc.parseSpawnEntity(payload);
    if (!p) return null;
    const ent = {
      id: p.entityId,
      type: p.type,
      uuid: p.uuid,
      x: 0,
      y: 0,
      z: 0,
      posKnown: false,
      vel: p.velocity,
      rot: { pitch: p.pitch, yaw: p.yaw, headPitch: p.headPitch },
      data: p.objectData,
      spawnTime: Date.now(),
    };
    updatePos(ent, p.x, p.y, p.z);
    set(p.entityId, ent);
    return null;
  }

  function handleDestroy(payload) {
    const p = lc.parseEntityDestroy(payload);
    if (!p) return null;
    for (const id of p.entityIds) {
      remove(id);
    }
    return null;
  }

  function applyDelta(id, dx, dy, dz) {
    const e = get(id);
    if (!e || !e.posKnown) return;
    updatePos(e, e.x + dx / 4096, e.y + dy / 4096, e.z + dz / 4096);
  }

  function handleRelMove(payload) {
    const p = lc.parseRelEntityMove(payload);
    if (!p) return null;
    applyDelta(p.entityId, p.dx, p.dy, p.dz);
    return suffixForIds([p.entityId]);
  }

  function handleMoveLook(payload) {
    const p = lc.parseEntityMoveLook(payload);
    if (!p) return null;
    applyDelta(p.entityId, p.dx, p.dy, p.dz);
    const e = get(p.entityId);
    if (e) {
      e.rot.yaw = p.yaw;
      e.rot.pitch = p.pitch;
    }
    return suffixForIds([p.entityId]);
  }

  function handleSyncPosition(payload) {
    const p = lc.parseSyncEntityPosition(payload);
    if (!p) return null;
    const e = get(p.entityId);
    if (e) {
      updatePos(e, p.x, p.y, p.z);
      e.rot.yaw = p.yaw;
      e.rot.pitch = p.pitch;
    }
    return suffixForIds([p.entityId]);
  }

  function handleTeleport(payload) {
    const p = lc.parseEntityTeleport(payload);
    if (!p) return null;
    const e = get(p.entityId);
    if (e) {
      updatePos(e, p.x, p.y, p.z);
      e.rot.yaw = p.yaw;
      e.rot.pitch = p.pitch;
    }
    return suffixForIds([p.entityId]);
  }

  function handleVelocity(payload) {
    const p = lc.parseEntityVelocity(payload);
    if (!p) return null;
    const e = get(p.entityId);
    if (e) {
      e.vel = p.velocity;
    }
    return suffixForIds([p.entityId]);
  }

  function handleHeadRotation(payload) {
    const p = lc.parseEntityHeadRotation(payload);
    if (!p) return null;
    const e = get(p.entityId);
    if (e) {
      e.rot.headYaw = p.headYaw;
    }
    return suffixForIds([p.entityId]);
  }

  function handleLook(payload) {
    const p = lc.parseEntityLook(payload);
    if (!p) return null;
    const e = get(p.entityId);
    if (e) {
      e.rot.yaw = p.yaw;
      e.rot.pitch = p.pitch;
    }
    return suffixForIds([p.entityId]);
  }

  function handleMetadata(payload) {
    const p = lc.parseEntityMetadata(payload);
    if (!p) return null;
    const e = get(p.entityId);
    if (e) {
      if (!e.metadata) e.metadata = {};
      for (const entry of p.metadata) {
        e.metadata[entry.key] = entry.value;
      }
    }
    return suffixForIds([p.entityId]);
  }

  function handleEquipment(payload) {
    const p = lc.parseEntityEquipment(payload);
    if (!p) return null;
    const e = get(p.entityId);
    if (e) {
      if (!e.equipment) e.equipment = {};
      for (const eq of p.equipments) {
        e.equipment[eq.slot] = { itemId: eq.itemId, itemCount: eq.itemCount };
      }
    }
    return suffixForIds([p.entityId]);
  }

  function handleEntityStatus(payload) {
    const p = lc.parseEntityStatus(payload);
    if (!p) return null;
    const e = get(p.entityId);
    if (e) {
      e.status = p.status;
    }
    return suffixForIds([p.entityId]);
  }

  function handleSetPassengers(payload) {
    const p = lc.parseSetPassengers(payload);
    if (!p) return null;
    const vehicle = get(p.entityId);
    if (vehicle) {
      vehicle.passengers = p.passengers;
    }
    const ids = [p.entityId, ...p.passengers];
    return suffixForIds(ids);
  }

  function handleAttachEntity(payload) {
    const p = lc.parseAttachEntity(payload);
    if (!p) return null;
    const attached = get(p.attachedId);
    if (attached) {
      attached.attachedTo = p.holdingId;
    }
    return suffixForIds([p.attachedId, p.holdingId]);
  }

  function handleUpdateAttributes(payload) {
    const p = lc.parseEntityUpdateAttributes(payload);
    if (!p) return null;
    const e = get(p.entityId);
    if (e) {
      if (!e.attributes) e.attributes = {};
      for (const prop of p.properties) {
        e.attributes[prop.key] = {
          keyName: prop.keyName,
          value: prop.value,
          modifiers: prop.modifiers
        };
      }
    }
    return suffixForIds([p.entityId]);
  }

  function handleEntityEffect(payload) {
    const p = lc.parseEntityEffect(payload);
    if (!p) return null;
    const e = get(p.entityId);
    if (e) {
      if (!e.effects) e.effects = {};
      e.effects[p.effectId] = {
        amplifier: p.amplifier,
        duration: p.duration,
        flags: p.flags
      };
    }
    return suffixForIds([p.entityId]);
  }

  function handleRemoveEntityEffect(payload) {
    const p = lc.parseRemoveEntityEffect(payload);
    if (!p) return null;
    const e = get(p.entityId);
    if (e && e.effects) {
      delete e.effects[p.effectId];
    }
    return suffixForIds([p.entityId]);
  }

  /** @type {Record<string, (payload: Buffer) => string | null>} */
  const handlers = {
    spawn_entity: handleSpawn,
    entity_destroy: handleDestroy,
    rel_entity_move: handleRelMove,
    entity_move_look: handleMoveLook,
    sync_entity_position: handleSyncPosition,
    entity_teleport: handleTeleport,
    entity_velocity: handleVelocity,
    entity_metadata: handleMetadata,
    entity_update_attributes: handleUpdateAttributes,
    entity_equipment: handleEquipment,
    entity_head_rotation: handleHeadRotation,
    entity_look: handleLook,
    entity_effect: handleEntityEffect,
    remove_entity_effect: handleRemoveEntityEffect,
    entity_status: handleEntityStatus,
    set_passengers: handleSetPassengers,
    attach_entity: handleAttachEntity,
  };

  function noteS2c(packetName, payload) {
    if (!packetName || !Buffer.isBuffer(payload)) return null;
    const fn = handlers[packetName];
    if (!fn) return null;
    try {
      return fn(payload);
    } catch {
      return null;
    }
  }

  return { noteS2c, get, entities };
}

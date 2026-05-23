const { createLogger } = require('../utils/logger');
const { degreesToByteAngle, sanitizeSpawnEntity } = require('../utils/angles');
const log = createLogger('EntityCache');

function applyVelocity(spawnData, velocity) {
  spawnData.velocity = {
    x: velocity?.x ?? 0,
    y: velocity?.y ?? 0,
    z: velocity?.z ?? 0,
  };
  spawnData.velocityKnown = true;
}

/**
 * Tracks entities received from the server.
 * Stores spawn data, metadata, equipment, effects, and position updates.
 */
class EntityCache {
  constructor() {
    /** @type {Map<number, object>} entityId -> entity state */
    this.entities = new Map();
  }

  handleSpawnEntity(data) {
    const entityId = data.entityId;
    const spawnData = { ...data, velocityKnown: false };
    // spawn_entity.velocity is lpVec3 on 1.21.9+ wire — not used for Anvil Motion.
    delete spawnData.velocity;
    this.entities.set(entityId, {
      spawnData,
      metadata: null,
      equipment: null,
      effects: [],
      passengers: null,
    });
  }

  handleEntityMetadata(data) {
    const entity = this.entities.get(data.entityId);
    if (!entity) return;
    if (!entity.metadata?.metadata) {
      entity.metadata = { entityId: data.entityId, metadata: [] };
    }
    for (const entry of data.metadata ?? []) {
      const idx = entity.metadata.metadata.findIndex((e) => e.key === entry.key);
      if (idx >= 0) entity.metadata.metadata[idx] = entry;
      else entity.metadata.metadata.push(entry);
    }
  }

  handleEntityEquipment(data) {
    const entity = this.entities.get(data.entityId);
    if (!entity) return;
    if (!entity.equipment?.slots) {
      entity.equipment = { entityId: data.entityId, slots: new Map() };
    }
    for (const { slot, item } of data.equipments ?? []) {
      if (!item || item.itemId < 0 || item.itemCount <= 0) {
        entity.equipment.slots.delete(slot);
      } else {
        entity.equipment.slots.set(slot, item);
      }
    }
  }

  handleEntityEffect(data) {
    const entity = this.entities.get(data.entityId);
    if (entity) {
      // Replace existing effect of same type, or add
      entity.effects = entity.effects.filter(e => e.effectId !== data.effectId);
      entity.effects.push({ ...data });
    }
  }

  handleRemoveEntityEffect(data) {
    const entity = this.entities.get(data.entityId);
    if (entity) {
      entity.effects = entity.effects.filter(e => e.effectId !== data.effectId);
    }
  }

  handleEntityDestroy(data) {
    // data.entityIds is an array of entity IDs to destroy
    const ids = data.entityIds || [];
    for (const id of ids) {
      this.entities.delete(id);
    }
  }

  handleSetPassengers(data) {
    const entity = this.entities.get(data.entityId);
    if (entity) {
      entity.passengers = { ...data };
    }
  }

  /**
   * Update entity position from various movement packets.
   * We update the spawn data so replay sends correct initial position.
   */
  handleEntityPosition(data) {
    const entity = this.entities.get(data.entityId);
    if (entity && entity.spawnData) {
      if (data.x !== undefined) entity.spawnData.x = data.x;
      if (data.y !== undefined) entity.spawnData.y = data.y;
      if (data.z !== undefined) entity.spawnData.z = data.z;
      if (data.yaw !== undefined) entity.spawnData.yaw = data.yaw;
      if (data.pitch !== undefined) entity.spawnData.pitch = data.pitch;
    }
  }

  handleSyncEntityPosition(data) {
    const entity = this.entities.get(data.entityId);
    if (entity && entity.spawnData) {
      if (data.x !== undefined) entity.spawnData.x = data.x;
      if (data.y !== undefined) entity.spawnData.y = data.y;
      if (data.z !== undefined) entity.spawnData.z = data.z;
      if (data.dx !== undefined || data.dy !== undefined || data.dz !== undefined) {
        applyVelocity(entity.spawnData, { x: data.dx, y: data.dy, z: data.dz });
      }
      if (data.onGround !== undefined) entity.spawnData.onGround = data.onGround;
      // sync_entity_position yaw/pitch are f32 degrees — never treat as i8 bytes (90° ≠ byte 90)
      if (data.yaw !== undefined) entity.spawnData.yaw = degreesToByteAngle(data.yaw);
      if (data.pitch !== undefined) entity.spawnData.pitch = degreesToByteAngle(data.pitch);
    }
  }

  /** Ignored for Anvil Motion — lpVec3 entity_velocity is not mapped to blocks/tick. */
  handleEntityVelocity(_data) {}

  handleRelEntityMove(data) {
    const entity = this.entities.get(data.entityId);
    if (entity && entity.spawnData) {
      // delta values are fixed-point (divided by 4096)
      entity.spawnData.x += (data.dX || 0) / 4096;
      entity.spawnData.y += (data.dY || 0) / 4096;
      entity.spawnData.z += (data.dZ || 0) / 4096;
    }
  }

  handleEntityMoveLook(data) {
    const entity = this.entities.get(data.entityId);
    if (entity && entity.spawnData) {
      entity.spawnData.x += (data.dX || 0) / 4096;
      entity.spawnData.y += (data.dY || 0) / 4096;
      entity.spawnData.z += (data.dZ || 0) / 4096;
      if (data.yaw !== undefined) entity.spawnData.yaw = data.yaw;
      if (data.pitch !== undefined) entity.spawnData.pitch = data.pitch;
    }
  }

  handleEntityTeleport(data) {
    const entity = this.entities.get(data.entityId);
    if (entity && entity.spawnData) {
      entity.spawnData.x = data.x;
      entity.spawnData.y = data.y;
      entity.spawnData.z = data.z;
      entity.spawnData.yaw = data.yaw;
      entity.spawnData.pitch = data.pitch;
    }
  }

  /** @param {object|null} equipment */
  static equipmentSnapshot(equipment) {
    if (!equipment?.slots) return equipment;
    return {
      entityId: equipment.entityId,
      equipments: [...equipment.slots.entries()].map(([slot, item]) => ({
        slot,
        item,
      })),
    };
  }

  /**
   * Get all entities for replay.
   * Returns spawn packets + metadata + equipment + effects.
   */
  getAllEntities() {
    const result = [];
    for (const [entityId, entity] of this.entities) {
      result.push({
        entityId,
        spawnData: sanitizeSpawnEntity(entity.spawnData),
        metadata: entity.metadata,
        equipment: EntityCache.equipmentSnapshot(entity.equipment),
        effects: entity.effects,
        passengers: entity.passengers,
      });
    }
    return result;
  }

  /**
   * Remove the bot's own entity ID from tracking
   * (the player entity is handled separately via player state).
   */
  removePlayerEntity(entityId) {
    this.entities.delete(entityId);
  }

  get size() {
    return this.entities.size;
  }

  clear() {
    this.entities.clear();
  }
}

module.exports = { EntityCache };

const { createLogger } = require('../utils/logger');
const { toByteAngle, sanitizeSpawnEntity } = require('../utils/angles');
const log = createLogger('EntityCache');

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
    this.entities.set(entityId, {
      spawnData: { ...data },
      metadata: null,
      equipment: null,
      effects: [],
      passengers: null,
    });
  }

  handleEntityMetadata(data) {
    const entity = this.entities.get(data.entityId);
    if (entity) {
      entity.metadata = { ...data };
    }
  }

  handleEntityEquipment(data) {
    const entity = this.entities.get(data.entityId);
    if (entity) {
      entity.equipment = { ...data };
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
      // sync_entity_position uses f32 degrees; spawn_entity expects i8 byte angles
      if (data.yaw !== undefined) entity.spawnData.yaw = toByteAngle(data.yaw);
      if (data.pitch !== undefined) entity.spawnData.pitch = toByteAngle(data.pitch);
    }
  }

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
        equipment: entity.equipment,
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

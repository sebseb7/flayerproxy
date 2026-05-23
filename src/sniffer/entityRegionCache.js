'use strict';

const { EntityCache } = require('../state/EntityCache');

/**
 * @typedef {{ chunkX: number, chunkZ: number }} ChunkCoord
 * @typedef {{ crossed: boolean, chunks: ChunkCoord[] }} EntityChunkTouch
 */

/**
 * Tracks world entities grouped by chunk column for entities/*.mca export.
 */
class EntityRegionCache {
  constructor() {
    this.entities = new EntityCache();
    /** @type {Map<number, string>} network id → "chunkX,chunkZ" */
    this.entityChunk = new Map();
    /** @type {number|null} */
    this.playerEntityId = null;
  }

  setPlayerEntityId(entityId) {
    this.playerEntityId = entityId;
  }

  /** @returns {ChunkCoord} */
  _chunkAt(x, z) {
    return {
      chunkX: Math.floor(x / 16),
      chunkZ: Math.floor(z / 16),
    };
  }

  _key(chunkX, chunkZ) {
    return `${chunkX},${chunkZ}`;
  }

  /** @returns {EntityChunkTouch} */
  _emptyTouch() {
    return { crossed: false, chunks: [] };
  }

  /** @param {number} entityId @returns {ChunkCoord|null} */
  _chunkForEntity(entityId) {
    const key = this.entityChunk.get(entityId);
    if (!key) return null;
    const [chunkX, chunkZ] = key.split(',').map(Number);
    return { chunkX, chunkZ };
  }

  /**
   * Update chunk index after a position change.
   * @returns {EntityChunkTouch}
   */
  _updatePosition(entityId, x, z) {
    const prev = this.entityChunk.get(entityId);
    const { chunkX, chunkZ } = this._chunkAt(x, z);
    const next = this._key(chunkX, chunkZ);

    if (prev === next) {
      return { crossed: false, chunks: [{ chunkX, chunkZ }] };
    }

    if (prev) {
      const [px, pz] = prev.split(',').map(Number);
      this.entityChunk.set(entityId, next);
      return {
        crossed: true,
        chunks: [
          { chunkX: px, chunkZ: pz },
          { chunkX, chunkZ },
        ],
      };
    }

    this.entityChunk.set(entityId, next);
    return { crossed: false, chunks: [{ chunkX, chunkZ }] };
  }

  _positionFromSpawn(entityId) {
    const ent = this.entities.entities.get(entityId);
    const s = ent?.spawnData;
    if (!s || s.x == null) return null;
    return { x: s.x, z: s.z };
  }

  /** @param {number} entityId @returns {EntityChunkTouch} */
  _touchEntity(entityId) {
    const c = this._chunkForEntity(entityId);
    return c ? { crossed: false, chunks: [c] } : this._emptyTouch();
  }

  /**
   * @param {object} data - spawn_entity
   * @returns {ChunkCoord|null}
   */
  handleSpawnEntity(data) {
    if (data.entityId == null || data.entityId === this.playerEntityId) return null;
    this.entities.handleSpawnEntity(data);
    const { chunkX, chunkZ } = this._chunkAt(data.x, data.z);
    this.entityChunk.set(data.entityId, this._key(chunkX, chunkZ));
    return { chunkX, chunkZ };
  }

  /**
   * @param {object} data - entity_destroy
   * @returns {ChunkCoord[]}
   */
  handleEntityDestroy(data) {
    const touched = new Set();
    for (const id of data.entityIds ?? []) {
      const key = this.entityChunk.get(id);
      if (key) touched.add(key);
      this.entityChunk.delete(id);
    }
    this.entities.handleEntityDestroy(data);
    return [...touched].map((key) => {
      const [chunkX, chunkZ] = key.split(',').map(Number);
      return { chunkX, chunkZ };
    });
  }

  /** @param {object} data @returns {EntityChunkTouch} */
  handleEntityMetadata(data) {
    if (data.entityId === this.playerEntityId) return this._emptyTouch();
    this.entities.handleEntityMetadata(data);
    return this._touchEntity(data.entityId);
  }

  handleEntityEquipment(data) {
    if (data.entityId === this.playerEntityId) return this._emptyTouch();
    this.entities.handleEntityEquipment(data);
    return this._touchEntity(data.entityId);
  }

  handleEntityEffect(data) {
    if (data.entityId === this.playerEntityId) return this._emptyTouch();
    this.entities.handleEntityEffect(data);
    return this._touchEntity(data.entityId);
  }

  handleRemoveEntityEffect(data) {
    if (data.entityId === this.playerEntityId) return this._emptyTouch();
    this.entities.handleRemoveEntityEffect(data);
    return this._touchEntity(data.entityId);
  }

  handleSetPassengers(data) {
    if (data.entityId === this.playerEntityId) return this._emptyTouch();
    this.entities.handleSetPassengers(data);
    return this._touchEntity(data.entityId);
  }

  handleEntityTeleport(data) {
    if (data.entityId === this.playerEntityId) return this._emptyTouch();
    this.entities.handleEntityTeleport(data);
    return this._updatePosition(data.entityId, data.x, data.z);
  }

  handleRelEntityMove(data) {
    if (data.entityId === this.playerEntityId) return this._emptyTouch();
    this.entities.handleRelEntityMove(data);
    const pos = this._positionFromSpawn(data.entityId);
    return pos ? this._updatePosition(data.entityId, pos.x, pos.z) : this._touchEntity(data.entityId);
  }

  handleEntityMoveLook(data) {
    if (data.entityId === this.playerEntityId) return this._emptyTouch();
    this.entities.handleEntityMoveLook(data);
    const pos = this._positionFromSpawn(data.entityId);
    return pos ? this._updatePosition(data.entityId, pos.x, pos.z) : this._touchEntity(data.entityId);
  }

  handleSyncEntityPosition(data) {
    if (data.entityId === this.playerEntityId) return this._emptyTouch();
    this.entities.handleSyncEntityPosition(data);
    return this._updatePosition(data.entityId, data.x, data.z);
  }

  handleEntityHeadRotation(data) {
    if (data.entityId === this.playerEntityId) return this._emptyTouch();
    const entity = this.entities.entities.get(data.entityId);
    if (entity?.spawnData && data.headYaw != null) {
      entity.spawnData.headPitch = data.headYaw;
    }
    return this._touchEntity(data.entityId);
  }

  hasChunk(chunkX, chunkZ) {
    const key = this._key(chunkX, chunkZ);
    for (const k of this.entityChunk.values()) {
      if (k === key) return true;
    }
    return false;
  }

  _trackedRow(entityId) {
    const ent = this.entities.entities.get(entityId);
    if (!ent) return null;
    const { sanitizeSpawnEntity } = require('../utils/angles');
    return {
      entityId,
      spawnData: sanitizeSpawnEntity(ent.spawnData),
      metadata: ent.metadata,
      equipment: ent.equipment,
      effects: ent.effects,
      passengers: ent.passengers,
    };
  }

  getExportEntry(chunkX, chunkZ) {
    const key = this._key(chunkX, chunkZ);
    const rows = [];
    for (const row of this.entities.getAllEntities()) {
      if (this.entityChunk.get(row.entityId) !== key) continue;
      if (row.entityId === this.playerEntityId) continue;
      rows.push(row);
    }
    const { topLevelEntitiesForChunk } = require('./entityAnvilEncode');
    const entities = topLevelEntitiesForChunk(rows);
    return {
      x: chunkX,
      z: chunkZ,
      entities,
      lookupEntity: (id) => this._trackedRow(id),
    };
  }

  /**
   * Server unload_chunk only drops client terrain — entities stay in the world.
   * Keep cache entries so entities/*.mca export is not wiped when columns go out of view.
   */
  unloadChunk(_chunkX, _chunkZ) {}

  get size() {
    return this.entities.size;
  }

  clear() {
    this.entities.clear();
    this.entityChunk.clear();
  }
}

module.exports = { EntityRegionCache };

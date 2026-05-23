'use strict';

const fs = require('fs');
const path = require('path');
const { ChunkCache } = require('../state/ChunkCache');
const { EntityRegionCache } = require('./entityRegionCache');
const {
  worldBoundsForDimension,
  dimensionNameFromLogin,
} = require('../state/chunkMerge');
const { IncrementalRegionExporter } = require('./incrementalRegionExporter');
const { IncrementalEntityExporter } = require('./incrementalEntityExporter');
const { writeLevelDat } = require('./levelSaveExporter');

const CHUNK_PACKETS = new Set([
  'map_chunk',
  'chunk_data',
  'level_chunk_with_light',
  'update_light',
  'unload_chunk',
  'block_change',
  'multi_block_change',
  'tile_entity_data',
]);

const ENTITY_PACKETS = new Set([
  'spawn_entity',
  'entity_metadata',
  'entity_equipment',
  'entity_effect',
  'remove_entity_effect',
  'entity_destroy',
  'set_passengers',
  'entity_teleport',
  'rel_entity_move',
  'entity_move_look',
  'sync_entity_position',
  'entity_head_rotation',
]);

const META_PACKETS = new Set([
  'login',
  'position',
  'spawn_position',
  'update_time',
  'respawn',
]);

/**
 * Accumulates decoded terrain + entities from sniffer S2C play packets and writes
 * region/ and entities/ incrementally.
 */
class SnifferWorldCapture {
  constructor(opts) {
    this.version = opts.version ?? '1.21.10';
    this.enabled = opts.enabled !== false;
    this.loginPacket = null;
    this.playerEntityId = null;
    this.position = null;
    this.spawnPosition = null;
    this.time = null;
    this.dimensionName = 'overworld';
    this.worldDir = null;
    this.levelName = null;
    this.exporter = null;
    this.entityExporter = null;

    this.chunks = new ChunkCache(opts.maxChunks ?? 8192, {
      version: this.version,
      retainAllChunks: true,
      getWorldBounds: () =>
        worldBoundsForDimension(this.version, this.dimensionName),
    });
    this.entities = new EntityRegionCache();
  }

  configureExport(opts) {
    const safeName =
      (opts.worldName ?? opts.sessionId)
        .replace(/[^\w.\- ]+/g, '_')
        .trim() || opts.sessionId;
    this.levelName = safeName;
    this.worldDir = path.join(path.resolve(opts.saveDir), safeName);
    fs.mkdirSync(this.worldDir, { recursive: true });

    const getWorldBounds = () =>
      worldBoundsForDimension(this.version, this.dimensionName);

    this.exporter = new IncrementalRegionExporter({
      worldDir: this.worldDir,
      version: this.version,
      dimensionName: this.dimensionName,
      getWorldBounds,
      onUnloadChunk: (x, z) => this.chunks.unloadChunk(x, z),
    });
    this.entityExporter = new IncrementalEntityExporter({
      worldDir: this.worldDir,
      version: this.version,
      dimensionName: this.dimensionName,
      getWorldBounds,
    });
    void this.exporter.ensureRegionDir();
    void this.entityExporter.ensureRegionDir();
  }

  handleServerPacket(name, data) {
    if (!this.enabled) return false;

    if (META_PACKETS.has(name)) {
      this._handleMeta(name, data);
      return true;
    }

    if (CHUNK_PACKETS.has(name)) {
      this._handleChunkPacket(name, data);
      return true;
    }

    if (ENTITY_PACKETS.has(name)) {
      this._handleEntityPacket(name, data);
      return true;
    }

    return false;
  }

  _handleChunkPacket(name, data) {
    switch (name) {
      case 'map_chunk':
      case 'chunk_data':
      case 'level_chunk_with_light':
        this.chunks.handleMapChunk(data);
        this._patchTerrainChunk(data.x, data.z);
        break;
      case 'update_light':
        this.chunks.handleUpdateLight(data);
        this._markTerrainDirty(data.chunkX, data.chunkZ);
        break;
      case 'unload_chunk':
        this._releaseChunk(data.chunkX, data.chunkZ);
        break;
      case 'block_change':
        this.chunks.handleBlockChange(data);
        this._markTerrainDirty(
          Math.floor(data.location.x / 16),
          Math.floor(data.location.z / 16),
        );
        break;
      case 'multi_block_change':
        this.chunks.handleMultiBlockChange(data);
        this._markTerrainDirty(
          data.chunkCoordinates?.x,
          data.chunkCoordinates?.z,
        );
        break;
      case 'tile_entity_data': {
        const loc = data.location;
        if (loc?.x != null) {
          this.chunks.handleTileEntityData(data);
          this._markTerrainDirty(
            Math.floor(loc.x / 16),
            Math.floor(loc.z / 16),
          );
        }
        break;
      }
      default:
        break;
    }
  }

  _handleEntityPacket(name, data) {
    switch (name) {
      case 'spawn_entity': {
        const at = this.entities.handleSpawnEntity(data);
        if (at) this._patchEntityChunk(at.chunkX, at.chunkZ, 'spawn');
        break;
      }
      case 'entity_destroy': {
        const touched = this.entities.handleEntityDestroy(data);
        this._patchEntityChunks(touched, 'destroy');
        break;
      }
      case 'entity_metadata':
        this._markEntityDirty(this.entities.handleEntityMetadata(data));
        break;
      case 'entity_equipment':
        this._markEntityDirty(this.entities.handleEntityEquipment(data));
        break;
      case 'entity_effect':
        this._markEntityDirty(this.entities.handleEntityEffect(data));
        break;
      case 'remove_entity_effect':
        this._markEntityDirty(this.entities.handleRemoveEntityEffect(data));
        break;
      case 'set_passengers':
        this._markEntityDirty(this.entities.handleSetPassengers(data));
        break;
      case 'entity_teleport': {
        const touch = this.entities.handleEntityTeleport(data);
        this._markEntityDirty(touch);
        if (touch.crossed) this._patchEntityChunks(touch.chunks, 'cross');
        break;
      }
      case 'rel_entity_move':
        this._markEntityDirty(this.entities.handleRelEntityMove(data));
        break;
      case 'entity_move_look':
        this._markEntityDirty(this.entities.handleEntityMoveLook(data));
        break;
      case 'sync_entity_position':
        this._markEntityDirty(this.entities.handleSyncEntityPosition(data));
        break;
      case 'entity_head_rotation':
        this._markEntityDirty(this.entities.handleEntityHeadRotation(data));
        break;
      default:
        break;
    }
  }

  _handleMeta(name, data) {
    switch (name) {
      case 'login':
        this.loginPacket = { ...data };
        this.dimensionName = dimensionNameFromLogin(data);
        if (data.entityId != null) {
          this.playerEntityId = data.entityId;
          this.entities.setPlayerEntityId(data.entityId);
        }
        break;
      case 'position':
        this.position = { ...data };
        break;
      case 'spawn_position':
        this.spawnPosition = { ...data };
        break;
      case 'update_time':
        this.time = { ...data };
        break;
      case 'respawn':
        if (data.dimension) {
          this.dimensionName = dimensionNameFromLogin({ dimension: data.dimension });
        }
        break;
      default:
        break;
    }
  }

  _patchTerrainChunk(x, z) {
    if (!this.exporter || x == null || z == null) return;
    if (!this.chunks.hasChunk(x, z)) return;
    this.exporter.patchColumn(
      () => this.chunks.getExportEntry(x, z),
      'map_chunk',
    );
  }

  _markTerrainDirty(x, z) {
    if (!this.exporter || x == null || z == null) return;
    if (!this.chunks.hasChunk(x, z)) return;
    this.exporter.markColumnDirty(x, z, () => this.chunks.getExportEntry(x, z));
  }

  _patchEntityChunk(x, z, reason) {
    if (!this.entityExporter || x == null || z == null) return;
    this.entityExporter.patchColumn(
      () => this.entities.getExportEntry(x, z),
      reason,
    );
  }

  /** @param {{ chunkX: number, chunkZ: number }[]} coords */
  _patchEntityChunks(coords, reason) {
    if (!this.entityExporter || !coords?.length) return;
    const seen = new Set();
    this.entityExporter.patchColumns(() => {
      const entries = [];
      for (const { chunkX, chunkZ } of coords) {
        const key = `${chunkX},${chunkZ}`;
        if (seen.has(key)) continue;
        seen.add(key);
        const entry = this.entities.getExportEntry(chunkX, chunkZ);
        if (entry) entries.push(entry);
      }
      return entries;
    }, reason);
  }

  /** @param {{ crossed: boolean, chunks: { chunkX: number, chunkZ: number }[] }} touch */
  _markEntityDirty(touch) {
    if (!touch?.chunks?.length) return;
    const seen = new Set();
    for (const { chunkX, chunkZ } of touch.chunks) {
      const key = `${chunkX},${chunkZ}`;
      if (seen.has(key)) continue;
      seen.add(key);
      if (!this.entities.hasChunk(chunkX, chunkZ)) continue;
      this.entityExporter.markColumnDirty(chunkX, chunkZ, () =>
        this.entities.getExportEntry(chunkX, chunkZ),
      );
    }
  }

  _releaseChunk(x, z) {
    if (this.exporter) this.exporter.releaseColumn(x, z);
    if (this.entityExporter) this.entityExporter.releaseColumn(x, z);
  }

  get chunkCount() {
    return this.exporter?.columnsWritten ?? this.chunks.size;
  }

  get regionChunkCount() {
    return this.exporter?.columnsWritten ?? 0;
  }

  get entityRegionChunkCount() {
    return this.entityExporter?.columnsWritten ?? 0;
  }

  getExportSnapshot() {
    const spawn = this._resolveSpawn();
    const seed = this._resolveSeed();
    const gamemode = this.loginPacket?.worldState?.gamemode ?? 'survival';
    const gameType = gamemode === 'creative' ? 1 : gamemode === 'adventure' ? 2 : gamemode === 'spectator' ? 3 : 0;

    return {
      version: this.version,
      dimensionName: this.dimensionName,
      levelName: this.levelName,
      seed,
      spawn,
      gameType,
      hardcore: this.loginPacket?.isHardcore === true,
      time: this.time?.age,
      dayTime: this.time?.time,
      chunks: this.chunks.getAllPackets(),
      worldBounds: worldBoundsForDimension(this.version, this.dimensionName),
    };
  }

  _patchAllEntityColumns(reason) {
    if (!this.entityExporter) return;
    const coords = [];
    const seen = new Set();
    for (const key of this.entities.entityChunk.values()) {
      if (seen.has(key)) continue;
      seen.add(key);
      const [chunkX, chunkZ] = key.split(',').map(Number);
      coords.push({ chunkX, chunkZ });
    }
    this._patchEntityChunks(coords, reason);
  }

  async finalizeExport() {
    if (!this.exporter || !this.worldDir) return null;

    this._patchAllEntityColumns('finalize');

    await Promise.all([
      this.exporter.flushAll(),
      this.entityExporter?.flushAll(),
    ]);

    const chunkCount = this.exporter.columnsWritten;
    const entityChunks = this.entityExporter?.columnsWritten ?? 0;
    if (!chunkCount && !entityChunks) return null;

    const snapshot = this.getExportSnapshot();
    await writeLevelDat(path.join(this.worldDir, 'level.dat'), {
      ...snapshot,
      minecraftVersion: snapshot.version,
      levelName: this.levelName ?? path.basename(this.worldDir),
    });

    return {
      worldDir: this.worldDir,
      chunkCount,
      entityChunkCount: entityChunks,
      regionDir: path.join(this.worldDir, 'region'),
      entitiesDir: path.join(this.worldDir, 'entities'),
    };
  }

  _resolveSpawn() {
    const pos = this.position;
    if (pos?.x != null) {
      return {
        x: Math.floor(pos.x),
        y: Math.floor(pos.y),
        z: Math.floor(pos.z),
      };
    }

    const global = this.spawnPosition?.globalPos;
    const loc = global?.location;
    if (loc?.x != null) {
      return {
        x: Math.floor(loc.x),
        y: Math.floor(loc.y ?? 64),
        z: Math.floor(loc.z),
      };
    }

    const death = this.loginPacket?.worldState?.death?.location;
    if (death?.x != null) {
      return {
        x: Math.floor(death.x),
        y: Math.floor(death.y ?? 64),
        z: Math.floor(death.z),
      };
    }

    const first = this.chunks.getAllPackets()[0];
    if (first) {
      return { x: first.x * 16 + 8, y: 64, z: first.z * 16 + 8 };
    }

    return { x: 0, y: 64, z: 0 };
  }

  _resolveSeed() {
    const hashed = this.loginPacket?.worldState?.hashedSeed;
    if (Array.isArray(hashed) && hashed.length >= 2) {
      return [Number(hashed[0]) | 0, Number(hashed[1]) | 0];
    }
    if (typeof hashed === 'bigint') {
      return [
        Number(hashed & 0xffffffffn) | 0,
        Number((hashed >> 32n) & 0xffffffffn) | 0,
      ];
    }
    return [Date.now() | 0, 0];
  }
}

module.exports = {
  SnifferWorldCapture,
  CHUNK_PACKETS,
  ENTITY_PACKETS,
  META_PACKETS,
};

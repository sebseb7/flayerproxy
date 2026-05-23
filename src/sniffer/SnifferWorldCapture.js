'use strict';

const { ChunkCache } = require('../state/ChunkCache');
const {
  worldBoundsForDimension,
  dimensionNameFromLogin,
} = require('../state/chunkMerge');

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

const META_PACKETS = new Set([
  'login',
  'position',
  'spawn_position',
  'update_time',
  'respawn',
]);

/**
 * Accumulates decoded terrain from sniffer S2C play packets for level export.
 */
class SnifferWorldCapture {
  /**
   * @param {object} opts
   * @param {string} opts.version - minecraft-data version (e.g. 1.21.10)
   * @param {number} [opts.maxChunks=8192]
   */
  constructor(opts) {
    this.version = opts.version ?? '1.21.10';
    this.enabled = opts.enabled !== false;
    this.loginPacket = null;
    this.position = null;
    this.spawnPosition = null;
    this.time = null;
    this.dimensionName = 'overworld';

    this.chunks = new ChunkCache(opts.maxChunks ?? 8192, {
      version: this.version,
      retainAllChunks: true,
      getWorldBounds: () =>
        worldBoundsForDimension(this.version, this.dimensionName),
    });
  }

  /**
   * @param {string} name
   * @param {object} data
   * @returns {boolean} whether the packet was handled
   */
  handleServerPacket(name, data) {
    if (!this.enabled) return false;

    if (META_PACKETS.has(name)) {
      this._handleMeta(name, data);
      return true;
    }

    if (!CHUNK_PACKETS.has(name)) return false;

    switch (name) {
      case 'map_chunk':
      case 'chunk_data':
      case 'level_chunk_with_light':
        this.chunks.handleMapChunk(data);
        break;
      case 'update_light':
        this.chunks.handleUpdateLight(data);
        break;
      case 'unload_chunk':
        this.chunks.handleUnloadChunk(data);
        break;
      case 'block_change':
        this.chunks.handleBlockChange(data);
        break;
      case 'multi_block_change':
        this.chunks.handleMultiBlockChange(data);
        break;
      case 'tile_entity_data':
        this.chunks.handleTileEntityData(data);
        break;
      default:
        break;
    }
    return true;
  }

  _handleMeta(name, data) {
    switch (name) {
      case 'login':
        this.loginPacket = { ...data };
        this.dimensionName = dimensionNameFromLogin(data);
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

  get chunkCount() {
    return this.chunks.size;
  }

  /**
   * Metadata + columns for Anvil export.
   */
  getExportSnapshot() {
    const spawn = this._resolveSpawn();
    const seed = this._resolveSeed();
    const gamemode = this.loginPacket?.worldState?.gamemode ?? 'survival';
    const gameType = gamemode === 'creative' ? 1 : gamemode === 'adventure' ? 2 : gamemode === 'spectator' ? 3 : 0;

    return {
      version: this.version,
      dimensionName: this.dimensionName,
      levelName: null,
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
      // Keep signed i32 halves — >>> 0 breaks nbt.long (e.g. hashedSeed from login).
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

module.exports = { SnifferWorldCapture, CHUNK_PACKETS, META_PACKETS };

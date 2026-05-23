const { createLogger } = require('../utils/logger');
const { isChunkWithinViewDistance } = require('../utils/positionSync');
const {
  worldBoundsForDimension,
  loadColumnFromMapChunk,
  exportMapChunkPacket,
  normalizeMapChunkPacket,
  applyBlockChange,
  applyUpdateLight,
  applyMultiBlockChange,
  applyTileEntityData,
} = require('./chunkMerge');

const log = createLogger('ChunkCache');

/**
 * Caches chunk column data keyed by "x,z".
 * Block and light updates are merged into the column; replay encodes map_chunk from column export.
 */
class ChunkCache {
  /**
   * @param {number} maxChunks
   * @param {{ version?: string, getWorldBounds?: () => { minY: number, worldHeight: number } }} [options]
   */
  constructor(maxChunks = 1024, options = {}) {
    this.maxChunks = maxChunks;
    this.retainAllChunks = options.retainAllChunks === true;
    this.version = options.version ?? '1.21.10';
    this.getWorldBounds = options.getWorldBounds ?? (() => worldBoundsForDimension(this.version));
    /** @type {Map<string, object>} key "x,z" -> stored chunk */
    this.chunks = new Map();
    /** Track access order for LRU eviction */
    this.accessOrder = [];
  }

  /** @returns {string} */
  _key(x, z) {
    return `${x},${z}`;
  }

  /**
   * Remove cached chunks outside the server's ChunkTrackingView for this center.
   * @returns {number} chunks removed
   */
  forgetOutsideView(centerChunkX, centerChunkZ, viewDistance) {
    if (centerChunkX == null || centerChunkZ == null || viewDistance == null) return 0;

    let removed = 0;
    for (const key of [...this.chunks.keys()]) {
      const [x, z] = key.split(',').map(Number);
      if (!isChunkWithinViewDistance(centerChunkX, centerChunkZ, x, z, viewDistance)) {
        this.chunks.delete(key);
        this.accessOrder = this.accessOrder.filter((k) => k !== key);
        removed++;
      }
    }
    if (removed > 0) {
      log.debug(
        `Forgot ${removed} chunk(s) outside view (${centerChunkX}, ${centerChunkZ}) distance ${viewDistance}`
      );
    }
    return removed;
  }

  /**
   * Store a map_chunk when it lies within view distance of the current center.
   * @param {object} [view] - { centerChunkX, centerChunkZ, viewDistance }
   */
  handleMapChunk(data, _rawBuffer, view) {
    if (
      !this.retainAllChunks &&
      view?.centerChunkX != null &&
      view.viewDistance != null
    ) {
      if (
        !isChunkWithinViewDistance(
          view.centerChunkX,
          view.centerChunkZ,
          data.x,
          data.z,
          view.viewDistance
        )
      ) {
        return;
      }
      this.forgetOutsideView(view.centerChunkX, view.centerChunkZ, view.viewDistance);
    }

    const key = this._key(data.x, data.z);
    const packetData = normalizeMapChunkPacket(structuredClone(data));
    this.chunks.set(key, {
      packetData,
      column: null,
    });
    this._touch(key);
    this._evictIfNeeded();
  }

  /**
   * Merge update_light into a cached chunk column (requires map_chunk already cached).
   */
  handleUpdateLight(data) {
    const key = this._key(data.chunkX, data.chunkZ);
    const stored = this.chunks.get(key);
    if (!stored) return;
    try {
      const column = this._ensureColumn(stored);
      applyUpdateLight(column, data);
      this._syncPacketFromColumn(stored);
      this._touch(key);
    } catch (err) {
      log.warn(
        `update_light merge failed for ${data.chunkX},${data.chunkZ}: ${err.message}`
      );
    }
  }

  /**
   * Handle chunk unload — remove from cache.
   */
  handleUnloadChunk(data) {
    const key = this._key(data.chunkX, data.chunkZ);
    this.chunks.delete(key);
    this.accessOrder = this.accessOrder.filter(k => k !== key);
  }

  /**
   * Merge block_change into the cached chunk column.
   */
  handleBlockChange(data) {
    const chunkX = Math.floor(data.location.x / 16);
    const chunkZ = Math.floor(data.location.z / 16);
    const stored = this.chunks.get(this._key(chunkX, chunkZ));
    if (!stored) return;
    try {
      const column = this._ensureColumn(stored);
      applyBlockChange(column, data);
      this._syncPacketFromColumn(stored);
      this._touch(this._key(chunkX, chunkZ));
    } catch (err) {
      log.warn(`block_change merge failed for ${chunkX},${chunkZ}: ${err.message}`);
    }
  }

  handleMultiBlockChange(data) {
    const chunkX = data.chunkCoordinates?.x;
    const chunkZ = data.chunkCoordinates?.z;
    if (chunkX == null || chunkZ == null) return;
    const key = this._key(chunkX, chunkZ);
    const stored = this.chunks.get(key);
    if (!stored) return;
    if (stored.packetData.x !== chunkX || stored.packetData.z !== chunkZ) return;
    try {
      const column = this._ensureColumn(stored);
      applyMultiBlockChange(column, data);
      this._syncPacketFromColumn(stored);
      this._touch(key);
    } catch (err) {
      log.warn(`multi_block_change merge failed for ${chunkX},${chunkZ}: ${err.message}`);
    }
  }

  /**
   * Merge tile_entity_data (sign text, chest contents, etc.) into a cached column.
   */
  handleTileEntityData(data) {
    const loc = data.location;
    if (!loc || loc.x == null) return;
    const chunkX = Math.floor(loc.x / 16);
    const chunkZ = Math.floor(loc.z / 16);
    const key = this._key(chunkX, chunkZ);
    const stored = this.chunks.get(key);
    if (!stored) return;
    try {
      const column = this._ensureColumn(stored);
      applyTileEntityData(column, data, chunkX, chunkZ);
      this._syncPacketFromColumn(stored);
      this._touch(key);
    } catch (err) {
      log.warn(`tile_entity_data merge failed for ${chunkX},${chunkZ}: ${err.message}`);
    }
  }

  /**
   * @param {object} stored
   * @returns {import('prismarine-chunk').Chunk}
   */
  _ensureColumn(stored) {
    if (!stored.column) {
      stored.column = loadColumnFromMapChunk(
        stored.packetData,
        this.version,
        this.getWorldBounds()
      );
    }
    return stored.column;
  }

  /**
   * @param {object} stored
   */
  _syncPacketFromColumn(stored) {
    stored.packetData = exportMapChunkPacket(stored.column, stored.packetData);
  }

  _buildChunkEntry(stored) {
    if (stored.column) {
      stored.packetData = exportMapChunkPacket(stored.column, stored.packetData);
    } else {
      normalizeMapChunkPacket(stored.packetData);
    }
    return {
      packetData: stored.packetData,
    };
  }

  /**
   * Get cached chunks within view distance of a center, sorted nearest-first.
   * Vanilla ignores map_chunk outside the current view center — always set
   * update_view_position before sending these.
   */
  getChunksForReplay(centerChunkX, centerChunkZ, viewDistance) {
    const result = [];
    for (const [key, stored] of this.chunks) {
      const [x, z] = key.split(',').map(Number);
      if (!isChunkWithinViewDistance(centerChunkX, centerChunkZ, x, z, viewDistance)) {
        continue;
      }
      result.push(this._buildChunkEntry(stored));
    }
    result.sort((a, b) => {
      const distA = Math.max(
        Math.abs(a.packetData.x - centerChunkX),
        Math.abs(a.packetData.z - centerChunkZ)
      );
      const distB = Math.max(
        Math.abs(b.packetData.x - centerChunkX),
        Math.abs(b.packetData.z - centerChunkZ)
      );
      return distA - distB;
    });
    return result;
  }

  hasChunkAtBlock(x, z) {
    const chunkX = Math.floor(x / 16);
    const chunkZ = Math.floor(z / 16);
    return this.chunks.has(this._key(chunkX, chunkZ));
  }

  hasChunk(chunkX, chunkZ) {
    return this.chunks.has(this._key(chunkX, chunkZ));
  }

  /**
   * @returns {{ x: number, z: number, packetData: object, column: import('prismarine-chunk').Chunk }|null}
   */
  getExportEntry(chunkX, chunkZ) {
    const stored = this.chunks.get(this._key(chunkX, chunkZ));
    if (!stored) return null;
    normalizeMapChunkPacket(stored.packetData);
    const column = this._ensureColumn(stored);
    this._syncPacketFromColumn(stored);
    return {
      x: chunkX,
      z: chunkZ,
      packetData: stored.packetData,
      column,
    };
  }

  /**
   * Drop in-memory column after incremental exporter idle timeout.
   * @param {number} chunkX
   * @param {number} chunkZ
   */
  unloadChunk(chunkX, chunkZ) {
    const key = this._key(chunkX, chunkZ);
    this.chunks.delete(key);
    this.accessOrder = this.accessOrder.filter((k) => k !== key);
  }

  get size() {
    return this.chunks.size;
  }

  /**
   * Chunks for reference-format world export (merged prismarine columns).
   * @returns {{ x: number, z: number, packetData: object, column: import('prismarine-chunk').Chunk }[]}
   */
  getAllPackets() {
    const result = [];
    for (const [key, stored] of this.chunks) {
      const [x, z] = key.split(',').map(Number);
      normalizeMapChunkPacket(stored.packetData);
      result.push({
        x,
        z,
        packetData: stored.packetData,
        column: this._ensureColumn(stored),
      });
    }
    return result;
  }

  _touch(key) {
    this.accessOrder = this.accessOrder.filter(k => k !== key);
    this.accessOrder.push(key);
  }

  _evictIfNeeded() {
    while (this.chunks.size > this.maxChunks && this.accessOrder.length > 0) {
      const oldest = this.accessOrder.shift();
      this.chunks.delete(oldest);
      log.debug(`Evicted chunk ${oldest} (cache full: ${this.chunks.size}/${this.maxChunks})`);
    }
  }

  clear() {
    this.chunks.clear();
    this.accessOrder = [];
  }
}

module.exports = { ChunkCache };

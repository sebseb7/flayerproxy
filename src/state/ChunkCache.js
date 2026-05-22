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
} = require('./chunkMerge');

const log = createLogger('ChunkCache');

/**
 * Caches chunk column data keyed by "x,z".
 * Block and light updates are merged into the column; replay sends map_chunk only.
 */
class ChunkCache {
  /**
   * @param {number} maxChunks
   * @param {{ version?: string, getWorldBounds?: () => { minY: number, worldHeight: number } }} [options]
   */
  constructor(maxChunks = 1024, options = {}) {
    this.maxChunks = maxChunks;
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
  handleMapChunk(data, rawBuffer, view) {
    if (view?.centerChunkX != null && view.viewDistance != null) {
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
    this.chunks.set(key, {
      packetData: normalizeMapChunkPacket(structuredClone(data)),
      rawBuffer: rawBuffer ? Buffer.from(rawBuffer) : null,
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
    } catch (err) {
      log.warn(`multi_block_change merge failed for ${chunkX},${chunkZ}: ${err.message}`);
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
      stored.rawBuffer = null;
    }
    return stored.column;
  }

  /**
   * @param {object} stored
   */
  _syncPacketFromColumn(stored) {
    stored.packetData = exportMapChunkPacket(stored.column, stored.packetData);
    stored.rawBuffer = null;
  }

  _buildChunkEntry(chunkData) {
    return {
      packetData: chunkData.packetData,
      rawMapChunkBuffer: chunkData.rawBuffer,
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

  get size() {
    return this.chunks.size;
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

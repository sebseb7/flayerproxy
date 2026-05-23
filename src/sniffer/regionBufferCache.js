'use strict';

const fs = require('fs');
const path = require('path');
const { createLogger } = require('../utils/logger');
const {
  encodeChunkPayload,
  loadRegionBuffer,
  patchRegionBuffer,
} = require('./regionFile');

const log = createLogger('RegionExport');

/** Disk write only after this many ms without a patch to the region. */
const DEFAULT_REGION_IDLE_MS = 10_000;

/**
 * @typedef {object} RegionFlushState
 * @property {boolean} writing
 * @property {ReturnType<typeof setTimeout>|null} timer
 * @property {Promise<void>|null} inFlight
 */

/**
 * Keeps region files in memory; writes to disk only after an idle period with no patches.
 */
class RegionBufferCache {
  /**
   * @param {object} opts
   * @param {string} opts.regionDir
   * @param {number} [opts.regionIdleMs] - ms without patches before flushing to disk
   */
  constructor(opts) {
    this.regionDir = opts.regionDir;
    this.regionIdleMs = opts.regionIdleMs ?? opts.flushDelayMs ?? DEFAULT_REGION_IDLE_MS;
    this.encodePayload = opts.encodePayload ?? encodeChunkPayload;
    this.compressionType = opts.compressionType ?? 1;
    /** @type {Map<string, Buffer>} */
    this.buffers = new Map();
    /** @type {Set<string>} */
    this.dirty = new Set();
    /** @type {Map<string, number>} patches since last successful flush */
    this.pendingCounts = new Map();
    /** @type {Map<string, RegionFlushState>} */
    this.states = new Map();
  }

  regionPath(chunkX, chunkZ) {
    return path.join(
      this.regionDir,
      `r.${chunkX >> 5}.${chunkZ >> 5}.mca`,
    );
  }

  /** @param {string} regionPath @returns {RegionFlushState} */
  _state(regionPath) {
    let st = this.states.get(regionPath);
    if (!st) {
      st = { writing: false, timer: null, inFlight: null };
      this.states.set(regionPath, st);
    }
    return st;
  }

  _regionLabel(regionPath) {
    const rx = path.basename(regionPath).match(/r\.(-?\d+)\./)?.[1];
    const rz = path.basename(regionPath).match(/r\.\-?\d+\.(-?\d+)/)?.[1];
    return `r.${rx}.${rz}`;
  }

  _clearTimer(regionPath) {
    const st = this._state(regionPath);
    if (st.timer) {
      clearTimeout(st.timer);
      st.timer = null;
    }
  }

  /**
   * Reset idle timer; flush to disk only after {@link regionIdleMs} with no patches.
   * @param {string} regionPath
   */
  _scheduleIdleFlush(regionPath) {
    const st = this._state(regionPath);
    this._clearTimer(regionPath);

    if (st.writing) return;

    st.timer = setTimeout(() => {
      st.timer = null;
      void this.flushRegion(regionPath, false);
    }, this.regionIdleMs);
  }

  /**
   * @param {number} chunkX
   * @param {number} chunkZ
   * @param {object} chunkTag
   * @returns {string} regionPath
   */
  _patchChunkInMemory(chunkX, chunkZ, chunkTag) {
    const regionPath = this.regionPath(chunkX, chunkZ);
    const payload = this.encodePayload(chunkTag);

    let buf = this.buffers.get(regionPath);
    if (!buf) {
      buf = loadRegionBuffer(regionPath);
      this.buffers.set(regionPath, buf);
    }

    buf = patchRegionBuffer(buf, chunkX, chunkZ, payload, this.compressionType);
    this.buffers.set(regionPath, buf);
    this.dirty.add(regionPath);
    this.pendingCounts.set(
      regionPath,
      (this.pendingCounts.get(regionPath) ?? 0) + 1,
    );

    return regionPath;
  }

  /**
   * @param {number} chunkX
   * @param {number} chunkZ
   * @param {object} chunkTag
   */
  patchChunk(chunkX, chunkZ, chunkTag) {
    const regionPath = this._patchChunkInMemory(chunkX, chunkZ, chunkTag);
    this._scheduleIdleFlush(regionPath);
    return regionPath;
  }

  /**
   * Apply many chunk patches; one idle timer reset per touched region file.
   * @param {{ chunkX: number, chunkZ: number, chunkTag: object }[]} updates
   * @returns {{ columns: number, byRegion: Map<string, number> }} label → column count
   */
  patchChunks(updates) {
    /** @type {Set<string>} */
    const touchedPaths = new Set();
    /** @type {Map<string, number>} */
    const byRegion = new Map();

    for (const { chunkX, chunkZ, chunkTag } of updates) {
      const regionPath = this._patchChunkInMemory(chunkX, chunkZ, chunkTag);
      touchedPaths.add(regionPath);
      const label = this._regionLabel(regionPath);
      byRegion.set(label, (byRegion.get(label) ?? 0) + 1);
    }

    for (const regionPath of touchedPaths) {
      this._scheduleIdleFlush(regionPath);
    }

    return { columns: updates.length, byRegion };
  }

  _pendingCount(regionPath) {
    return this.pendingCounts.get(regionPath) ?? 0;
  }

  /**
   * @param {string} regionPath
   * @param {boolean} [sync=false]
   */
  async flushRegion(regionPath, sync = false) {
    this._clearTimer(regionPath);
    const st = this._state(regionPath);

    if (st.writing && st.inFlight) {
      await st.inFlight;
    }

    if (st.writing) {
      return st.inFlight ?? Promise.resolve();
    }

    if (!this.dirty.has(regionPath) || this._pendingCount(regionPath) === 0) {
      return;
    }

    if (sync) {
      await this._runFlush(regionPath, true);
      return;
    }

    if (st.inFlight) {
      return st.inFlight;
    }

    st.inFlight = this._runFlush(regionPath, false);
    return st.inFlight;
  }

  /**
   * @param {string} regionPath
   * @param {boolean} sync
   */
  async _runFlush(regionPath, sync) {
    const st = this._state(regionPath);
    const buf = this.buffers.get(regionPath);
    if (!buf) return;

    const count = this._pendingCount(regionPath);
    if (count === 0) {
      this.dirty.delete(regionPath);
      return;
    }

    this.pendingCounts.set(regionPath, 0);
    st.writing = true;

    const label = this._regionLabel(regionPath);
    const t0 = performance.now();

    try {
      await fs.promises.mkdir(path.dirname(regionPath), { recursive: true });
      if (sync) {
        fs.writeFileSync(regionPath, buf);
      } else {
        await fs.promises.writeFile(regionPath, buf);
      }
      log.info(
        `Region ${label} → disk (${(performance.now() - t0).toFixed(1)} ms, ${count} chunk${count === 1 ? '' : 's'})`,
      );
    } catch (err) {
      log.warn(`Region flush failed ${regionPath}: ${err.message}`);
      this.pendingCounts.set(regionPath, this._pendingCount(regionPath) + count);
    } finally {
      st.writing = false;
      st.inFlight = null;

      if (this._pendingCount(regionPath) > 0) {
        this._scheduleIdleFlush(regionPath);
      } else {
        this.dirty.delete(regionPath);
      }
    }
  }

  flushRegionSync(regionPath) {
    return this.flushRegion(regionPath, true);
  }

  async flushAll() {
    for (const regionPath of [...this.states.keys()]) {
      this._clearTimer(regionPath);
    }
    for (const regionPath of [...this.dirty]) {
      await this.flushRegion(regionPath, true);
    }
    await Promise.all(
      [...this.states.values()]
        .map((st) => st.inFlight)
        .filter(Boolean),
    );
  }
}

module.exports = {
  RegionBufferCache,
  DEFAULT_REGION_IDLE_MS,
  /** @deprecated use DEFAULT_REGION_IDLE_MS */
  DEFAULT_FLUSH_DELAY_MS: DEFAULT_REGION_IDLE_MS,
};

'use strict';

const fs = require('fs');
const path = require('path');
const { createLogger } = require('../utils/logger');
const { RegionBufferCache } = require('./regionBufferCache');

const SYNC_INTERVAL_MS = 10_000;

/**
 * Shared incremental region writer: patch columns into RAM, debounce all disk flushes.
 * Hot map tracks columns with pending updates; the sync tick patches and releases them.
 */
class McaIncrementalExporter {
  /**
   * @param {object} opts
   * @param {string} opts.worldDir
   * @param {string} opts.regionSubdir - e.g. 'region' or 'entities'
   * @param {string} opts.version
   * @param {string} opts.logModule
   * @param {string} opts.columnLabel - log prefix, e.g. 'Chunk' or 'Entity chunk'
   * @param {(entry: object, encodeOpts: object) => object} opts.encodeColumnTag
   * @param {() => object} [opts.getEncodeOpts]
   * @param {(x: number, z: number) => void} [opts.onUnloadColumn] - server unload_chunk only
   * @param {number} [opts.regionIdleMs] - ms without patches before region → disk
   * @param {number} [opts.syncIntervalMs] - hot-column sync interval (entities default 2s)
   */
  constructor(opts) {
    this.log = createLogger(opts.logModule ?? 'RegionExport');
    this.columnLabel = opts.columnLabel ?? 'Chunk';
    this.encodeColumnTag = opts.encodeColumnTag;
    this.getEncodeOpts = opts.getEncodeOpts ?? (() => ({}));
    this.onUnloadColumn = opts.onUnloadColumn ?? (() => {});
    this.syncIntervalMs = opts.syncIntervalMs ?? SYNC_INTERVAL_MS;

    this.regionDir = path.join(opts.worldDir, opts.regionSubdir);
    this.regionCache = new RegionBufferCache({
      regionDir: this.regionDir,
      regionIdleMs: opts.regionIdleMs ?? opts.regionFlushDelayMs,
      encodePayload: opts.encodePayload,
      compressionType: opts.compressionType,
    });

    /** @type {Map<string, { dirty: boolean, getEntry: () => object|null }>} */
    this.hot = new Map();
    this.syncTimer = null;
    this.columnsWritten = 0;
    this._writeQueue = Promise.resolve();
  }

  _key(x, z) {
    return `${x},${z}`;
  }

  _enqueue(fn) {
    const job = this._writeQueue.then(fn);
    this._writeQueue = job.catch((err) => {
      this.log.warn(`${this.columnLabel} write queue: ${err.message}`);
    });
    return job;
  }

  /**
   * First-time or full refresh encode (map_chunk / spawn / destroy / cross).
   * Does not join the periodic hot sync loop.
   * @param {() => object|null} getEntry
   * @param {string} reason
   */
  patchColumn(getEntry, reason) {
    this._enqueue(async () => {
      const entry = typeof getEntry === 'function' ? getEntry() : getEntry;
      if (!entry) return;
      await this._patchOne(entry, reason);
    });
  }

  /**
   * Patch several columns in one queue job (grouped by region file).
   * @param {(() => object[]) | object[]} getEntries
   * @param {string} reason
   */
  patchColumns(getEntries, reason) {
    this._enqueue(async () => {
      const entries =
        typeof getEntries === 'function' ? getEntries() : getEntries;
      const logEach = reason !== 'sync' && reason !== 'flush' && reason !== 'finalize';
      const { columns, byRegion } = await this._patchBatch(entries, reason, {
        logEach,
      });
      if (columns > 0 && !logEach && columns > 1) {
        this.log.debug(
          `${this.columnLabel}: patched ${columns} column(s) in ${byRegion.size} region(s) [${this._formatRegionSummary(byRegion)}] (${reason})`,
        );
      }
    });
  }

  /**
   * Column had a mergeable update (block change, metadata, …).
   * Patched and released from hot on the next sync tick.
   */
  markColumnDirty(x, z, getEntry) {
    const key = this._key(x, z);

    let rec = this.hot.get(key);
    if (!rec) {
      rec = { dirty: true, getEntry };
      this.hot.set(key, rec);
      this._ensureSyncLoop();
    } else {
      rec.dirty = true;
      rec.getEntry = getEntry;
    }
  }

  async _patchOne(entry, reason) {
    const { columns } = await this._patchBatch([entry], reason, { logEach: true });
    return columns;
  }

  /**
   * @param {object[]} entries
   * @param {string} reason
   * @param {{ logEach?: boolean }} [opts]
   * @returns {Promise<{ columns: number, byRegion: Map<string, number> }>}
   */
  async _patchBatch(entries, reason, opts = {}) {
    const updates = [];
    for (const entry of entries) {
      if (!entry) continue;
      const chunkTag = this.encodeColumnTag(entry, this.getEncodeOpts());
      if (!chunkTag) continue;
      updates.push({
        chunkX: entry.x,
        chunkZ: entry.z,
        chunkTag,
      });
      this.columnsWritten++;
      if (opts.logEach) {
        this.log.debug(`${this.columnLabel} ${entry.x},${entry.z} patched (${reason})`);
      }
    }

    if (!updates.length) {
      return { columns: 0, byRegion: new Map() };
    }

    return this.regionCache.patchChunks(updates);
  }

  _formatRegionSummary(byRegion) {
    return [...byRegion.entries()]
      .map(([label, n]) => `${label} (${n})`)
      .join(', ');
  }

  _ensureSyncLoop() {
    if (this.syncTimer != null) return;
    this.syncTimer = setInterval(() => this._syncTick(), this.syncIntervalMs);
  }

  _syncTick() {
    /** @type {{ key: string, rec: { dirty: boolean, getEntry: () => object|null } }[]} */
    const batch = [];
    for (const [key, rec] of this.hot) {
      if (!rec.dirty) continue;
      batch.push({ key, rec });
    }
    if (!batch.length) return;

    this._enqueue(async () => {
      const t0 = performance.now();
      const entries = batch.map(({ rec }) => rec.getEntry()).filter(Boolean);
      const { columns, byRegion } = await this._patchBatch(entries, 'sync');
      for (const { key } of batch) {
        this.hot.delete(key);
      }
      if (columns > 0) {
        this.log.info(
          `${this.columnLabel}: synced and released ${columns} column(s) in ${byRegion.size} region(s) [${this._formatRegionSummary(byRegion)}] (${(performance.now() - t0).toFixed(0)} ms)`,
        );
      }
    });
  }

  /** Server unload_chunk: patch pending changes, drop hot tracking, free in-memory column. */
  releaseColumn(x, z) {
    const key = this._key(x, z);
    const rec = this.hot.get(key);
    if (rec) {
      if (rec.dirty) {
        const getEntry = rec.getEntry;
        this._enqueue(async () => {
          const entry = getEntry();
          if (entry) await this._patchOne(entry, 'unload');
        });
      }
      this.hot.delete(key);
    }
    this.onUnloadColumn(x, z);
  }

  async flushAll() {
    const batch = [];
    for (const [, rec] of this.hot) {
      if (!rec.dirty) continue;
      batch.push(rec);
      rec.dirty = false;
    }
    this.hot.clear();
    if (this.syncTimer != null) {
      clearInterval(this.syncTimer);
      this.syncTimer = null;
    }

    if (batch.length) {
      await this._enqueue(async () => {
        const t0 = performance.now();
        const entries = batch.map((rec) => rec.getEntry()).filter(Boolean);
        const { columns, byRegion } = await this._patchBatch(entries, 'flush');
        if (columns > 0) {
          this.log.info(
            `${this.columnLabel}: flushed ${columns} column(s) in ${byRegion.size} region(s) [${this._formatRegionSummary(byRegion)}] (${(performance.now() - t0).toFixed(0)} ms)`,
          );
        }
      });
    }

    await this._writeQueue;
    await this.regionCache.flushAll();
  }

  async ensureRegionDir() {
    await fs.promises.mkdir(this.regionDir, { recursive: true });
  }
}

module.exports = {
  McaIncrementalExporter,
  SYNC_INTERVAL_MS,
};

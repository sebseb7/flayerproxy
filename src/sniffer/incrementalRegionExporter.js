'use strict';

const { worldBoundsForDimension } = require('../state/chunkMerge');
const { encodeReferenceChunkTag } = require('./chunkRegionWrite');
const { McaIncrementalExporter } = require('./mcaIncrementalExporter');

/** Terrain `region/*.mca` incremental export. */
class IncrementalRegionExporter extends McaIncrementalExporter {
  constructor(opts) {
    const version = opts.version;
    const dimensionName = opts.dimensionName ?? 'overworld';
    const getWorldBounds =
      opts.getWorldBounds ??
      (() => worldBoundsForDimension(version, dimensionName));

    super({
      worldDir: opts.worldDir,
      regionSubdir: 'region',
      version,
      logModule: 'RegionExport',
      columnLabel: 'Chunk',
      regionIdleMs: opts.regionIdleMs ?? opts.regionFlushDelayMs,
      onUnloadColumn: opts.onUnloadChunk,
      getEncodeOpts: () => ({
        version,
        dimensionName,
        worldBounds: getWorldBounds(),
      }),
      encodeColumnTag: (entry, encodeOpts) =>
        encodeReferenceChunkTag(entry.packetData, entry.column ?? null, encodeOpts),
    });
  }
}

module.exports = { IncrementalRegionExporter };

'use strict';

const { worldBoundsForDimension } = require('../state/chunkMerge');
const { regionSubdirForDimension } = require('./dimensionStorage');
const { encodeReferenceChunkTag } = require('./chunkRegionWrite');
const { McaIncrementalExporter } = require('./mcaIncrementalExporter');

/** Terrain `region/*.mca` incremental export. */
class IncrementalRegionExporter extends McaIncrementalExporter {
  constructor(opts) {
    const version = opts.version;
    const getDimensionName =
      opts.getDimensionName ?? (() => opts.dimensionName ?? 'overworld');
    const getWorldBounds =
      opts.getWorldBounds ??
      (() => worldBoundsForDimension(version, getDimensionName()));

    super({
      worldDir: opts.worldDir,
      getRegionSubdir:
        opts.getRegionSubdir ??
        (() => regionSubdirForDimension(getDimensionName(), 'region')),
      version,
      logModule: 'RegionExport',
      columnLabel: 'Chunk',
      regionIdleMs: opts.regionIdleMs ?? opts.regionFlushDelayMs,
      onUnloadColumn: opts.onUnloadChunk,
      getEncodeOpts: () => ({
        version,
        dimensionName: getDimensionName(),
        worldBounds: getWorldBounds(),
      }),
      encodeColumnTag: (entry, encodeOpts) =>
        encodeReferenceChunkTag(entry.packetData, entry.column ?? null, encodeOpts),
    });
  }
}

module.exports = { IncrementalRegionExporter };

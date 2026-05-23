'use strict';

const { worldBoundsForDimension } = require('../state/chunkMerge');
const { regionSubdirForDimension } = require('./dimensionStorage');
const { encodeEntityRegionChunkTag } = require('./entityRegionWrite');
const { encodeEntityChunkPayload } = require('./regionFile');
const { McaIncrementalExporter } = require('./mcaIncrementalExporter');

const ENTITY_SYNC_INTERVAL_MS = 2_000;

/** Entity `entities/*.mca` incremental export. */
class IncrementalEntityExporter extends McaIncrementalExporter {
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
        (() => regionSubdirForDimension(getDimensionName(), 'entities')),
      version,
      logModule: 'EntityExport',
      columnLabel: 'Entity chunk',
      regionIdleMs: opts.regionIdleMs ?? opts.regionFlushDelayMs,
      syncIntervalMs: opts.syncIntervalMs ?? ENTITY_SYNC_INTERVAL_MS,
      encodePayload: encodeEntityChunkPayload,
      compressionType: 2,
      getEncodeOpts: () => ({
        version,
        dimensionName: getDimensionName(),
        worldBounds: getWorldBounds(),
      }),
      encodeColumnTag: (entry, encodeOpts) =>
        encodeEntityRegionChunkTag(entry, encodeOpts),
    });
  }
}

module.exports = { IncrementalEntityExporter };

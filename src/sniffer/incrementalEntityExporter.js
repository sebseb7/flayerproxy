'use strict';

const { worldBoundsForDimension } = require('../state/chunkMerge');
const { encodeEntityRegionChunkTag } = require('./entityRegionWrite');
const { encodeEntityChunkPayload } = require('./regionFile');
const { McaIncrementalExporter } = require('./mcaIncrementalExporter');

const ENTITY_SYNC_INTERVAL_MS = 2_000;

/** Entity `entities/*.mca` incremental export. */
class IncrementalEntityExporter extends McaIncrementalExporter {
  constructor(opts) {
    const version = opts.version;
    const dimensionName = opts.dimensionName ?? 'overworld';
    const getWorldBounds =
      opts.getWorldBounds ??
      (() => worldBoundsForDimension(version, dimensionName));

    super({
      worldDir: opts.worldDir,
      regionSubdir: 'entities',
      version,
      logModule: 'EntityExport',
      columnLabel: 'Entity chunk',
      regionIdleMs: opts.regionIdleMs ?? opts.regionFlushDelayMs,
      syncIntervalMs: opts.syncIntervalMs ?? ENTITY_SYNC_INTERVAL_MS,
      encodePayload: encodeEntityChunkPayload,
      compressionType: 2,
      getEncodeOpts: () => ({
        version,
        dimensionName,
        worldBounds: getWorldBounds(),
      }),
      encodeColumnTag: (entry, encodeOpts) =>
        encodeEntityRegionChunkTag(entry, encodeOpts),
    });
  }
}

module.exports = { IncrementalEntityExporter };

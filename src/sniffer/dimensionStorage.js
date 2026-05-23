'use strict';

/**
 * Vanilla Anvil folder for terrain or entity regions (relative to world root).
 * Overworld: region/ / entities/
 * Nether:    DIM-1/region/ / DIM-1/entities/
 * End:       DIM1/region/ / DIM1/entities/
 *
 * @param {string} dimensionName - with or without `minecraft:` prefix
 * @param {'region'|'entities'} kind
 * @returns {string}
 */
function regionSubdirForDimension(dimensionName, kind = 'region') {
  const key = String(dimensionName).replace(/^minecraft:/, '');
  const leaf = kind === 'entities' ? 'entities' : 'region';
  if (key === 'the_nether') return `DIM-1/${leaf}`;
  if (key === 'the_end') return `DIM1/${leaf}`;
  return leaf;
}

module.exports = { regionSubdirForDimension };

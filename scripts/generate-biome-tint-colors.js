#!/usr/bin/env node
'use strict';
/**
 * Build libchunk/src/biome_tint.c: per-biome grass/foliage RGB + state tint kinds.
 * Colormaps from minecraft-assets; climates from minecraft-data 1.21.10 + category downfall.
 */

const fs = require('fs');
const path = require('path');
const zlib = require('zlib');

const MC_VERSION = '1.21.10';
const mcData = require('minecraft-data')(MC_VERSION);
const TEXTURES = path.resolve(__dirname, '../../minecraft-assets/assets/minecraft/textures');
const OUT = path.join(__dirname, '../libchunk/src/biome_tint.c');

const DEFAULT_TEMP = 0.8;
const DEFAULT_DOWNFALL = 0.4;

/** Vanilla-style downfall when minecraft-data omits it (by category). */
const CATEGORY_DOWNFALL = {
  taiga: 0.4,
  extreme_hills: 0.3,
  jungle: 0.4,
  forest: 0.4,
  plains: 0.4,
  desert: 0,
  mushroom: 0.4,
  nether: 0,
  the_end: 0,
  beach: 0.4,
  river: 0.4,
  ocean: 0.4,
  snowy: 0.5,
  icy: 0.5,
  mesa: 0,
  savanna: 0,
  swamp: 0.5,
  underground: 0.4,
};

/**
 * Grass colors that do not come from grass.png colormap sampling (BiomeEffects.GrassColorModifier).
 * Swamp uses fixed sickly brown (#6A7039), not the bright teal colormap point at temp 0.8.
 */
/** Fixed grass RGB when vanilla skips colormap (modifiers / badlands). */
const BIOME_GRASS_RGB_OVERRIDE = {
  swamp: [0x6a, 0x70, 0x39],
  badlands: [0x90, 0x81, 0x4d],
  eroded_badlands: [0x90, 0x81, 0x4d],
  wooded_badlands: [0x90, 0x81, 0x4d],
  cherry_grove: [0xb6, 0xdb, 0x61],
  windswept_savanna: [0x82, 0xc2, 0x45],
};

function decodePng(buf) {
  if (buf.length < 24 || buf.readUInt32BE(0) !== 0x89504e47) throw new Error('not png');
  let pos = 8;
  let width = 0;
  let height = 0;
  let colorType = 0;
  const idats = [];
  while (pos + 12 <= buf.length) {
    const len = buf.readUInt32BE(pos);
    pos += 4;
    const type = buf.toString('ascii', pos, pos + 4);
    pos += 4;
    const data = buf.subarray(pos, pos + len);
    pos += len + 4;
    if (type === 'IHDR') {
      width = data.readUInt32BE(0);
      height = data.readUInt32BE(4);
      colorType = data[9];
    } else if (type === 'IDAT') idats.push(data);
    else if (type === 'IEND') break;
  }
  if (colorType !== 2 || width !== 256 || height !== 256) throw new Error('expected 256x256 RGB colormap');
  const stride = width * 3;
  const raw = zlib.inflateSync(Buffer.concat(idats));
  const out = Buffer.alloc(height * stride);
  let off = 0;
  let prev = null;
  for (let y = 0; y < height; y++) {
    const filter = raw[off++];
    const row = raw.subarray(off, off + stride);
    off += stride;
    const recon = Buffer.alloc(stride);
    for (let i = 0; i < stride; i++) {
      const left = i >= 3 ? recon[i - 3] : 0;
      const up = prev ? prev[i] : 0;
      const x = row[i];
      switch (filter) {
        case 0:
          recon[i] = x;
          break;
        case 1:
          recon[i] = (x + left) & 0xff;
          break;
        case 2:
          recon[i] = (x + up) & 0xff;
          break;
        default:
          recon[i] = (x + Math.floor((left + up) / 2)) & 0xff;
      }
    }
    recon.copy(out, y * stride);
    prev = recon;
  }
  return out;
}

/** Match BiomeColors.getColor: clamp climate to [0, 1] before grass.png / foliage.png lookup. */
function sampleColormap(mapData, temp, downfall) {
  const t = Math.min(1, Math.max(0, temp));
  const d = Math.min(1, Math.max(0, downfall));
  const adjustedDownfall = d * t;
  const x = Math.min(255, Math.max(0, Math.floor((1 - t) * 255)));
  const y = Math.min(255, Math.max(0, Math.floor((1 - adjustedDownfall) * 255)));
  const i = (y * 256 + x) * 3;
  return [mapData[i], mapData[i + 1], mapData[i + 2]];
}

const grassMap = decodePng(fs.readFileSync(path.join(TEXTURES, 'colormap/grass.png')));
const foliageMap = decodePng(fs.readFileSync(path.join(TEXTURES, 'colormap/foliage.png')));

function biomeDownfall(biome) {
  if (biome.category && CATEGORY_DOWNFALL[biome.category] !== undefined) {
    return CATEGORY_DOWNFALL[biome.category];
  }
  return biome.has_precipitation ? DEFAULT_DOWNFALL : 0;
}

let maxBiomeId = 0;
const biomes = Object.values(mcData.biomesByName || {});
for (const b of biomes) {
  if (b.id > maxBiomeId) maxBiomeId = b.id;
}
const grassRgb = new Uint8Array((maxBiomeId + 1) * 3);
const foliageRgb = new Uint8Array((maxBiomeId + 1) * 3);

for (const biome of biomes) {
  const temp = biome.temperature ?? DEFAULT_TEMP;
  const downfall = biomeDownfall(biome);
  const g = BIOME_GRASS_RGB_OVERRIDE[biome.name] ?? sampleColormap(grassMap, temp, downfall);
  const f = sampleColormap(foliageMap, temp, downfall);
  const o = biome.id * 3;
  grassRgb[o] = g[0];
  grassRgb[o + 1] = g[1];
  grassRgb[o + 2] = g[2];
  foliageRgb[o] = f[0];
  foliageRgb[o + 1] = f[1];
  foliageRgb[o + 2] = f[2];
}

const plains = mcData.biomesByName.plains;
const plainsId = plains ? plains.id : 40;
const plainsGrass = sampleColormap(grassMap, plains?.temperature ?? DEFAULT_TEMP, biomeDownfall(plains || { category: 'plains', has_precipitation: true }));
const plainsFoliage = sampleColormap(foliageMap, plains?.temperature ?? DEFAULT_TEMP, biomeDownfall(plains || { category: 'plains', has_precipitation: true }));

let maxState = 0;
for (const k of Object.keys(mcData.blocksByStateId)) {
  const id = Number(k);
  if (id > maxState) maxState = id;
}

const tintKind = new Uint8Array(maxState + 1);

function tintKindForBlock(block) {
  if (!block) return 0;
  if (block.name === 'grass_block') return 1;
  if (block.name.endsWith('_leaves')) {
    if (block.name.startsWith('birch_')) return 3;
    if (block.name.startsWith('spruce_') || block.name.startsWith('pine_')) return 4;
    return 2;
  }
  return 0;
}

for (let sid = 0; sid <= maxState; sid++) {
  tintKind[sid] = tintKindForBlock(mcData.blocksByStateId[sid]);
}

const lines = [];
lines.push('/* Generated by scripts/generate-biome-tint-colors.js — do not edit. */');
lines.push(`/* minecraft-data ${MC_VERSION} */`);
lines.push('#include "map_colors.h"');
lines.push('#include <stddef.h>');
lines.push('');
lines.push(`#define LC_BIOME_ID_MAX ${maxBiomeId}`);
lines.push(`#define LC_PLAINS_BIOME_ID ${plainsId}`);
lines.push('');
lines.push(`static const uint8_t LC_PLAINS_GRASS_RGB[3] = { ${plainsGrass.join(', ')} };`);
lines.push(`static const uint8_t LC_PLAINS_FOLIAGE_RGB[3] = { ${plainsFoliage.join(', ')} };`);
lines.push('');
lines.push(`static const uint8_t LC_BIOME_GRASS_RGB[(LC_BIOME_ID_MAX + 1) * 3] = {`);
for (let id = 0; id <= maxBiomeId; id++) {
  const o = id * 3;
  lines.push(`  ${grassRgb[o]}, ${grassRgb[o + 1]}, ${grassRgb[o + 2]},`);
}
lines.push('};');
lines.push('');
lines.push(`static const uint8_t LC_BIOME_FOLIAGE_RGB[(LC_BIOME_ID_MAX + 1) * 3] = {`);
for (let id = 0; id <= maxBiomeId; id++) {
  const o = id * 3;
  lines.push(`  ${foliageRgb[o]}, ${foliageRgb[o + 1]}, ${foliageRgb[o + 2]},`);
}
lines.push('};');
lines.push('');
lines.push('static const uint8_t LC_STATE_TINT_KIND[LC_STATE_MAP_MAX + 1] = {');
for (let i = 0; i <= maxState; i += 32) {
  const chunk = [];
  for (let j = i; j < i + 32 && j <= maxState; j++) chunk.push(String(tintKind[j]));
  lines.push('  ' + chunk.join(', ') + (i + 32 <= maxState ? ',' : ''));
}
lines.push('};');
lines.push('');
lines.push('static void lc_biome_rgb_lookup(const uint8_t *table, int32_t biome_id, uint8_t *r, uint8_t *g, uint8_t *b) {');
lines.push('  if (biome_id < 0 || biome_id > LC_BIOME_ID_MAX) biome_id = LC_PLAINS_BIOME_ID;');
lines.push('  const uint8_t *p = &table[(size_t)biome_id * 3];');
lines.push('  *r = p[0];');
lines.push('  *g = p[1];');
lines.push('  *b = p[2];');
lines.push('}');
lines.push('');
lines.push('static void lc_tint_ratio_rgb(uint8_t *r, uint8_t *g, uint8_t *b,');
lines.push('                                const uint8_t ref[3], const uint8_t tint[3]) {');
lines.push('  unsigned br = ref[0] ? ref[0] : 1u;');
lines.push('  unsigned bg = ref[1] ? ref[1] : 1u;');
lines.push('  unsigned bb = ref[2] ? ref[2] : 1u;');
lines.push('  *r = (uint8_t)(((unsigned)*r * tint[0]) / br);');
lines.push('  *g = (uint8_t)(((unsigned)*g * tint[1]) / bg);');
lines.push('  *b = (uint8_t)(((unsigned)*b * tint[2]) / bb);');
lines.push('}');
lines.push('');
lines.push('void lc_map_rgb_apply_biome_tint(int32_t state_id, int32_t biome_id, uint8_t *r, uint8_t *g, uint8_t *b) {');
lines.push('  if (!r || !g || !b || state_id < 0 || state_id > LC_STATE_MAP_MAX) return;');
lines.push('  uint8_t kind = LC_STATE_TINT_KIND[state_id];');
lines.push('  if (kind == 1) {');
lines.push('    /* grass_block top RGB is baked as plains; use biome grass color directly. */');
lines.push('    lc_biome_rgb_lookup(LC_BIOME_GRASS_RGB, biome_id, r, g, b);');
lines.push('  } else if (kind == 2) {');
lines.push('    uint8_t tr, tg, tb, tint[3];');
lines.push('    lc_biome_rgb_lookup(LC_BIOME_FOLIAGE_RGB, biome_id, &tr, &tg, &tb);');
lines.push('    tint[0] = tr;');
lines.push('    tint[1] = tg;');
lines.push('    tint[2] = tb;');
lines.push('    lc_tint_ratio_rgb(r, g, b, LC_PLAINS_FOLIAGE_RGB, tint);');
lines.push('  }');
lines.push('}');
lines.push('');

fs.writeFileSync(OUT, lines.join('\n'));
console.log('Wrote', OUT, 'biomes', maxBiomeId + 1, 'states', maxState + 1);

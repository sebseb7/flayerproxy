#!/usr/bin/env node
'use strict';
/**
 * Build libchunk/src/state_top_rgb.c from minecraft-assets block models (top face texture).
 * Uses minecraft-data 1.21.10 state ids (must match wire captures).
 */

const fs = require('fs');
const path = require('path');
const zlib = require('zlib');

const MC_VERSION = '1.21.10';
const mcData = require('minecraft-data')(MC_VERSION);
const ASSETS = path.resolve(__dirname, '../../minecraft-assets/assets/minecraft');
const MODELS = path.join(ASSETS, 'models');
const TEXTURES = path.join(ASSETS, 'textures');
const BLOCKSTATES = path.join(ASSETS, 'blockstates');
const OUT = path.join(__dirname, '../libchunk/src/state_top_rgb.c');

const WATER_BLOCKS = new Set(['water', 'bubble_column']);

/** Greyscale block textures that need a tint when the model omits tintindex. */
const GRASS_TOP_TEXTURES = new Set(['minecraft:block/grass_block_top', 'block/grass_block_top']);

/** Default plains biome for map columns without per-column biome data. */
const DEFAULT_GRASS_TEMP = 0.8;
const DEFAULT_GRASS_DOWNFALL = 0.4;

/** Fallback multipliers when colormap is unavailable. */
const TINT = {
  0: [0x91, 0xbd, 0x59], /* grass */
  1: [0x77, 0xab, 0x2f], /* foliage */
  2: [0x80, 0xa7, 0x55], /* birch */
  3: [0x61, 0x99, 0x61], /* spruce / pine */
  4: [0x8c, 0x9b, 0x8c], /* water (unused for top) */
};

const modelCache = new Map();
const blockstateCache = new Map();
const textureColorCache = new Map();
let grassColormapRgb = null;

function readJson(p) {
  return JSON.parse(fs.readFileSync(p, 'utf8'));
}

function modelFile(modelId) {
  let id = modelId;
  if (id.startsWith('minecraft:')) id = id.slice('minecraft:'.length);
  return path.join(MODELS, id + '.json');
}

function textureFile(textureRef) {
  let id = textureRef;
  if (id.startsWith('#')) return null;
  if (id.startsWith('minecraft:')) id = id.slice('minecraft:'.length);
  return path.join(TEXTURES, id + '.png');
}

function normalizeTextureId(textureRef) {
  if (!textureRef) return '';
  if (textureRef.startsWith('minecraft:')) return textureRef;
  if (textureRef.startsWith('block/')) return `minecraft:${textureRef}`;
  return `minecraft:block/${textureRef}`;
}

function loadGrassColormap() {
  if (grassColormapRgb) return grassColormapRgb;
  const file = path.join(TEXTURES, 'colormap/grass.png');
  const { width, height, data, colorType } = decodePng(fs.readFileSync(file));
  if (colorType !== 2 || width !== 256 || height !== 256) throw new Error('unexpected grass colormap');
  grassColormapRgb = { width, height, data };
  return grassColormapRgb;
}

/** Minecraft grass tint (BiomeColors): clamp temp/downfall, then downfall *= temperature. */
function sampleGrassColormap(temp = DEFAULT_GRASS_TEMP, downfall = DEFAULT_GRASS_DOWNFALL) {
  const map = loadGrassColormap();
  const t = Math.min(1, Math.max(0, temp));
  const d = Math.min(1, Math.max(0, downfall));
  const adjustedDownfall = d * t;
  const x = Math.min(255, Math.max(0, Math.floor((1 - t) * 255)));
  const y = Math.min(255, Math.max(0, Math.floor((1 - adjustedDownfall) * 255)));
  const i = (y * map.width + x) * 3;
  return [map.data[i], map.data[i + 1], map.data[i + 2]];
}

function effectiveTint(faceTint, textureRef) {
  if (faceTint !== undefined && faceTint !== null) return faceTint;
  const id = normalizeTextureId(textureRef);
  if (GRASS_TOP_TEXTURES.has(id)) return 0;
  if (id.endsWith('_leaves')) return 1;
  return undefined;
}

function topColorForState(block, props, face) {
  /* Map view: always biome grass green. snowy=true is common in cold biomes but is a
   * thin snow cap in-game; coloring it white makes the whole map look snow-covered. */
  if (block.name === 'grass_block') return sampleGrassColormap();
  if (!face || !face.texture) return [0x80, 0x80, 0x80];
  const texId = normalizeTextureId(face.texture);
  if (GRASS_TOP_TEXTURES.has(texId)) return sampleGrassColormap();
  const tint = effectiveTint(face.tint, face.texture);
  return averageTextureColor(face.texture, tint);
}

function unfilterScanline(filter, row, prev, bpp) {
  const out = Buffer.alloc(row.length);
  const n = bpp;
  for (let i = 0; i < row.length; i++) {
    const left = i >= n ? out[i - n] : 0;
    const up = prev ? prev[i] : 0;
    const upLeft = i >= n && prev ? prev[i - n] : 0;
    const x = row[i];
    switch (filter) {
      case 0:
        out[i] = x;
        break;
      case 1:
        out[i] = (x + left) & 0xff;
        break;
      case 2:
        out[i] = (x + up) & 0xff;
        break;
      case 3:
        out[i] = (x + Math.floor((left + up) / 2)) & 0xff;
        break;
      case 4: {
        const p = left + up - upLeft;
        const pa = Math.abs(p - left);
        const pb = Math.abs(p - up);
        const pc = Math.abs(p - upLeft);
        const pr = pa <= pb && pa <= pc ? left : pb <= pc ? up : upLeft;
        out[i] = (x + pr) & 0xff;
        break;
      }
      default:
        out[i] = x;
    }
  }
  return out;
}

function decodePng(buf) {
  if (buf.length < 24 || buf.readUInt32BE(0) !== 0x89504e47) throw new Error('not png');
  let pos = 8;
  let width = 0;
  let height = 0;
  let bitDepth = 0;
  let colorType = 0;
  const idats = [];
  let palette = null;
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
      bitDepth = data[8];
      colorType = data[9];
    } else if (type === 'PLTE') palette = data;
    else if (type === 'IDAT') idats.push(data);
    else if (type === 'IEND') break;
  }
  if (!width || !height) throw new Error('bad png');
  if (colorType === 3) {
    if (!palette) throw new Error('indexed png without PLTE');
  } else if (colorType === 6 || colorType === 2 || colorType === 4 || colorType === 0) {
    /* truecolor/grayscale: expand below */
  } else throw new Error('unsupported png colorType ' + colorType);

  if (colorType !== 3) {
    const stride = width * (colorType === 6 ? 4 : colorType === 2 ? 3 : colorType === 4 ? 2 : 1);
    const raw = zlib.inflateSync(Buffer.concat(idats));
    const out = Buffer.alloc(height * stride);
    let off = 0;
    let prev = null;
    for (let y = 0; y < height; y++) {
      const filter = raw[off++];
      const row = raw.subarray(off, off + stride);
      off += stride;
      const bpp = colorType === 6 ? 4 : colorType === 2 ? 3 : colorType === 4 ? 2 : 1;
      const recon = unfilterScanline(filter, row, prev, bpp);
      recon.copy(out, y * stride);
      prev = recon;
    }
    return { width, height, bitDepth: 8, data: out, palette: null, colorType };
  }

  const raw = zlib.inflateSync(Buffer.concat(idats));
  const stride = Math.ceil((width * bitDepth) / 8);
  const out = Buffer.alloc(height * stride);
  let off = 0;
  let prev = null;
  for (let y = 0; y < height; y++) {
    const filter = raw[off++];
    const row = raw.subarray(off, off + stride);
    off += stride;
    const recon = unfilterScanline(filter, row, prev, 1);
    recon.copy(out, y * stride);
    prev = recon;
  }
  return { width, height, bitDepth, data: out, palette };
}

function averageTextureColor(textureRef, tintIndex) {
  const key = textureRef + '|' + (tintIndex ?? '');
  if (textureColorCache.has(key)) return textureColorCache.get(key);

  const file = textureFile(textureRef);
  const fallback = [0x80, 0x80, 0x80];
  if (!file || !fs.existsSync(file)) {
    textureColorCache.set(key, fallback);
    return fallback;
  }
  let width;
  let height;
  let bitDepth;
  let data;
  let palette;
  let colorType;
  try {
    ({ width, height, bitDepth, data, palette, colorType } = decodePng(fs.readFileSync(file)));
  } catch (err) {
    textureColorCache.set(key, fallback);
    return fallback;
  }

  const rowStride = Math.ceil((width * (bitDepth || 8)) / 8);

  function pixelIndex(row, x) {
    if (bitDepth === 8) return data[row * rowStride + x];
    if (bitDepth === 4) {
      const b = data[row * rowStride + (x >> 1)];
      return x & 1 ? b & 0x0f : b >> 4;
    }
    if (bitDepth === 2) {
      const b = data[row * rowStride + (x >> 2)];
      const shift = 6 - (x & 3) * 2;
      return (b >> shift) & 3;
    }
    if (bitDepth === 1) {
      const b = data[row * rowStride + (x >> 3)];
      const shift = 7 - (x & 7);
      return (b >> shift) & 1;
    }
    return 0;
  }

  /* Animated strips (e.g. water_still 16×512): use first frame only. */
  const sampleH = height >= width * 2 && height % width === 0 ? width : height;

  let r = 0;
  let g = 0;
  let b = 0;
  let n = 0;
  for (let y = 0; y < sampleH; y++) {
    for (let x = 0; x < width; x++) {
      let pr, pg, pb, a = 255;
      if (colorType === 6) {
        const i = (y * width + x) * 4;
        pr = data[i];
        pg = data[i + 1];
        pb = data[i + 2];
        a = data[i + 3];
      } else if (colorType === 2) {
        const i = (y * width + x) * 3;
        pr = data[i];
        pg = data[i + 1];
        pb = data[i + 2];
      } else if (palette && palette.length) {
        const idx = pixelIndex(y, x);
        const pi = idx * 3;
        if (pi + 2 < palette.length) {
          pr = palette[pi];
          pg = palette[pi + 1];
          pb = palette[pi + 2];
        } else {
          pr = pg = pb = 128;
        }
      } else {
        const v = pixelIndex(y, x);
        pr = pg = pb = v;
      }
      if (a < 16) continue;
      if (tintIndex !== undefined && TINT[tintIndex]) {
        const t = TINT[tintIndex];
        pr = Math.round((pr * t[0]) / 255);
        pg = Math.round((pg * t[1]) / 255);
        pb = Math.round((pb * t[2]) / 255);
      }
      r += pr;
      g += pg;
      b += pb;
      n++;
    }
  }
  const rgb = n ? [Math.round(r / n), Math.round(g / n), Math.round(b / n)] : [0x80, 0x80, 0x80];
  textureColorCache.set(key, rgb);
  return rgb;
}

function resolveModel(modelId, seen = new Set()) {
  if (modelCache.has(modelId)) return modelCache.get(modelId);
  if (seen.has(modelId)) return { textures: {}, elements: [] };
  seen.add(modelId);

  const file = modelFile(modelId);
  if (!fs.existsSync(file)) {
    const empty = { textures: {}, elements: [] };
    modelCache.set(modelId, empty);
    return empty;
  }
  const json = readJson(file);
  let parent = { textures: {}, elements: [] };
  if (json.parent) parent = resolveModel(json.parent.startsWith('minecraft:') ? json.parent : `minecraft:${json.parent}`, seen);

  const textures = { ...parent.textures, ...(json.textures || {}) };
  const elements = json.elements && json.elements.length ? json.elements : parent.elements;
  const resolved = { textures, elements };
  modelCache.set(modelId, resolved);
  return resolved;
}

function resolveTextureRef(ref, textures) {
  if (!ref) return null;
  if (ref.startsWith('#')) {
    const key = ref.slice(1);
    const val = textures[key];
    if (!val) return null;
    return resolveTextureRef(val, textures);
  }
  return ref;
}

function topFaceFromModel(modelId) {
  const m = resolveModel(modelId);
  for (const el of m.elements) {
    const up = el.faces && el.faces.up;
    if (up && up.texture) {
      const ref = resolveTextureRef(up.texture, m.textures);
      return { texture: ref, tint: up.tintindex };
    }
  }
  const t = m.textures;
  const order = ['top', 'all', 'end', 'particle', 'side', 'north', 'south', 'east', 'west', 'bottom', 'down'];
  for (const k of order) {
    if (t[k]) {
      const ref = resolveTextureRef(t[k], t);
      if (ref) return { texture: ref, tint: undefined };
    }
  }
  return null;
}

function loadBlockstate(name) {
  if (blockstateCache.has(name)) return blockstateCache.get(name);
  const p = path.join(BLOCKSTATES, name + '.json');
  const bs = fs.existsSync(p) ? readJson(p) : null;
  blockstateCache.set(name, bs);
  return bs;
}

function propsMatchKey(key, props) {
  if (key === '') return true;
  return key.split(',').every((part) => {
    const eq = part.indexOf('=');
    if (eq < 0) return false;
    const k = part.slice(0, eq);
    const v = part.slice(eq + 1);
    return String(props[k]) === v;
  });
}

function propsMatchWhen(when, props) {
  if (!when) return true;
  if (typeof when === 'string') return propsMatchKey(when, props);
  return Object.entries(when).every(([k, v]) => {
    const vals = Array.isArray(v) ? v.map(String) : [String(v)];
    return vals.includes(String(props[k]));
  });
}

function normalizeApply(apply) {
  if (!apply) return [];
  return Array.isArray(apply) ? apply : [apply];
}

function pickModelForState(blockName, props) {
  const bs = loadBlockstate(blockName);
  if (!bs) return `minecraft:block/${blockName}`;

  if (bs.multipart) {
    for (const part of bs.multipart) {
      if (!propsMatchWhen(part.when, props)) continue;
      const apps = normalizeApply(part.apply);
      if (apps.length) return apps[0].model || apps[0];
    }
  }

  if (bs.variants) {
    for (const [key, val] of Object.entries(bs.variants)) {
      if (!propsMatchKey(key, props)) continue;
      const apps = normalizeApply(val);
      if (apps.length) {
        const m = apps[0];
        return typeof m === 'string' ? m : m.model;
      }
    }
  }

  return `minecraft:block/${blockName}`;
}

function propsFromStateIndex(block, stateId) {
  const states = block.states || [];
  if (!states.length) return {};
  let index = stateId - block.minStateId;
  const props = {};
  for (let i = states.length - 1; i >= 0; i--) {
    const s = states[i];
    const n = s.num_values;
    const valIndex = index % n;
    index = Math.floor(index / n);
    if (s.type === 'bool' && s.name === 'snowy')
      props[s.name] = valIndex ? 'false' : 'true';
    else if (s.type === 'bool') props[s.name] = valIndex ? 'true' : 'false';
    else props[s.name] = s.values[valIndex];
  }
  return props;
}

let maxState = 0;
for (const k of Object.keys(mcData.blocksByStateId)) {
  const id = Number(k);
  if (id > maxState) maxState = id;
}

const rgb = new Uint8Array((maxState + 1) * 3);
const isWater = new Uint8Array(maxState + 1);

for (let sid = 0; sid <= maxState; sid++) {
  const block = mcData.blocksByStateId[sid];
  const off = sid * 3;
  if (!block) {
    rgb[off] = rgb[off + 1] = rgb[off + 2] = 0;
    continue;
  }

  if (WATER_BLOCKS.has(block.name)) {
    isWater[sid] = 1;
    const face = topFaceFromModel('minecraft:block/water') || { texture: 'minecraft:block/water_still', tint: undefined };
    const tex = face.texture || 'minecraft:block/water_still';
    const [r, g, b] = averageTextureColor(tex, face.tint);
    rgb[off] = r;
    rgb[off + 1] = g;
    rgb[off + 2] = b;
    continue;
  }

  const props = propsFromStateIndex(block, sid);
  const modelId = pickModelForState(block.name, props);
  const face = topFaceFromModel(modelId);
  const [r, g, b] = topColorForState(block, props, face);
  rgb[off] = r;
  rgb[off + 1] = g;
  rgb[off + 2] = b;
}

const lines = [];
lines.push('/* Generated by scripts/generate-state-top-texture-colors.js — do not edit. */');
lines.push(`/* minecraft-data ${MC_VERSION}, textures from ../minecraft-assets */`);
lines.push('#include "map_colors.h"');
lines.push('#include <stddef.h>');
lines.push('');
lines.push('static const uint8_t LC_STATE_TOP_RGB[(LC_STATE_MAP_MAX + 1) * 3] = {');
for (let sid = 0; sid <= maxState; sid++) {
  const o = sid * 3;
  lines.push(`  ${rgb[o]}, ${rgb[o + 1]}, ${rgb[o + 2]},`);
}
lines.push('};');
lines.push('');
lines.push('static const uint8_t LC_STATE_IS_WATER[LC_STATE_MAP_MAX + 1] = {');
const perLine = 32;
for (let i = 0; i <= maxState; i += perLine) {
  const chunk = [];
  for (let j = i; j < i + perLine && j <= maxState; j++) chunk.push(String(isWater[j]));
  lines.push('  ' + chunk.join(', ') + (i + perLine <= maxState ? ',' : ''));
}
lines.push('};');
lines.push('');
lines.push('int lc_state_id_is_water(int32_t state_id) {');
lines.push('  if (state_id < 0 || state_id > LC_STATE_MAP_MAX) return 0;');
lines.push('  return LC_STATE_IS_WATER[state_id] != 0;');
lines.push('}');
lines.push('');
lines.push('void lc_state_id_top_rgb(int32_t state_id, uint8_t *r, uint8_t *g, uint8_t *b) {');
lines.push('  if (!r || !g || !b) return;');
lines.push('  if (state_id < 0 || state_id > LC_STATE_MAP_MAX) {');
lines.push('    *r = *g = *b = 0;');
lines.push('    return;');
lines.push('  }');
lines.push('  const uint8_t *p = &LC_STATE_TOP_RGB[(size_t)state_id * 3];');
lines.push('  *r = p[0];');
lines.push('  *g = p[1];');
lines.push('  *b = p[2];');
lines.push('}');
lines.push('');

fs.writeFileSync(OUT, lines.join('\n'));
console.log('Wrote', OUT, 'states', maxState + 1);

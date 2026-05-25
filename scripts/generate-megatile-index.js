#!/usr/bin/env node
/**
 * Write index.html in a megatile directory (x<world>_z<world>.avif, 512×512).
 * Pan/zoom map viewer; initial view centered on the tile nearest world (0, 0).
 * View state is stored in the URL hash (#px=&py=&s=) so reload restores pan/zoom.
 */
'use strict';

const fs = require('fs');
const path = require('path');

const dir = process.argv[2];
if (!dir) {
  console.error('Usage: generate-megatile-index.js <megatile_dir>');
  process.exit(1);
}

const TILE_PX = 512;
const PX_PER_BLOCK = 2;
const BLOCKS_PER_TILE = 256;

const re = /^x(-?\d+)_z(-?\d+)\.avif$/;

/** @type {{ wx: number, wz: number, file: string }[]} */
const tiles = [];
for (const name of fs.readdirSync(dir)) {
  const m = name.match(re);
  if (!m) continue;
  tiles.push({ wx: Number(m[1]), wz: Number(m[2]), file: name });
}

if (tiles.length === 0) {
  console.error(`no megatile AVIFs in ${dir}`);
  process.exit(1);
}

function tileContainsOrigin(t) {
  return t.wx <= 0 && 0 < t.wx + BLOCKS_PER_TILE && t.wz <= 0 && 0 < t.wz + BLOCKS_PER_TILE;
}

/** Squared block distance from world (0,0) to this tile's block rectangle. */
function distSqToOrigin(t) {
  const x1 = t.wx + BLOCKS_PER_TILE - 1;
  const z1 = t.wz + BLOCKS_PER_TILE - 1;
  const dx = t.wx > 0 ? t.wx : x1 < 0 ? -x1 : 0;
  const dz = t.wz > 0 ? t.wz : z1 < 0 ? -z1 : 0;
  return dx * dx + dz * dz;
}

let initial = tiles.find(tileContainsOrigin);
if (!initial) {
  initial = tiles.reduce((a, b) => (distSqToOrigin(a) <= distSqToOrigin(b) ? a : b));
}

const tilesJson = JSON.stringify(tiles);
const initialJson = JSON.stringify({ wx: initial.wx, wz: initial.wz });

const html = `<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <title>Megatile map</title>
  <style>
    * { box-sizing: border-box; margin: 0; padding: 0; }
    html, body { height: 100%; overflow: hidden; background: #1a1a1e; color: #e8e8ec; font: 13px/1.4 system-ui, sans-serif; }
    #viewport { position: relative; width: 100%; height: 100%; cursor: grab; touch-action: none; }
    #viewport.dragging { cursor: grabbing; }
    #layer { position: absolute; left: 0; top: 0; transform-origin: 0 0; will-change: transform; }
    #world { position: relative; }
    #world img {
      position: absolute;
      width: ${TILE_PX}px;
      height: ${TILE_PX}px;
      image-rendering: pixelated;
      image-rendering: crisp-edges;
      pointer-events: none;
      user-select: none;
    }
    #hud {
      position: fixed; left: 12px; bottom: 12px; z-index: 10;
      background: rgba(0,0,0,.72); padding: 8px 12px; border-radius: 8px;
      pointer-events: none; font-variant-numeric: tabular-nums;
    }
    #hint {
      position: fixed; right: 12px; top: 12px; z-index: 10;
      background: rgba(0,0,0,.55); padding: 6px 10px; border-radius: 6px;
      font-size: 12px; color: #aaa;
    }
  </style>
</head>
<body>
  <div id="viewport"><div id="layer"><div id="world"></div></div></div>
  <div id="hud">block — , —</div>
  <div id="hint">drag pan · wheel zoom</div>
  <script>
    const TILES = ${tilesJson};
    const INITIAL = ${initialJson};
    const TILE_PX = ${TILE_PX};
    const PX_PER_BLOCK = ${PX_PER_BLOCK};
    const BLOCKS_PER_TILE = ${BLOCKS_PER_TILE};

    const viewport = document.getElementById('viewport');
    const layer = document.getElementById('layer');
    const world = document.getElementById('world');
    const hud = document.getElementById('hud');

    let scale = 1;
    let panX = 0;
    let panY = 0;
    let dragging = false;
    let lastX = 0;
    let lastY = 0;

    for (const t of TILES) {
      const img = document.createElement('img');
      img.src = t.file;
      img.alt = t.file;
      img.loading = 'lazy';
      img.style.left = (t.wx * PX_PER_BLOCK) + 'px';
      img.style.top = (t.wz * PX_PER_BLOCK) + 'px';
      world.appendChild(img);
    }

    function applyTransform() {
      layer.style.transform = 'translate(' + panX + 'px,' + panY + 'px) scale(' + scale + ')';
      scheduleHashUpdate();
    }

    function parseHash() {
      const raw = location.hash.replace(/^#/, '');
      if (!raw) return null;
      const q = new URLSearchParams(raw);
      const px = q.get('px');
      const py = q.get('py');
      const s = q.get('s');
      if (px == null || py == null || s == null) return null;
      const pan = Number(px);
      const pyN = Number(py);
      const sc = Number(s);
      if (!Number.isFinite(pan) || !Number.isFinite(pyN) || !Number.isFinite(sc)) return null;
      return { panX: pan, panY: pyN, scale: Math.min(64, Math.max(0.05, sc)) };
    }

    let hashTimer = 0;
    function scheduleHashUpdate() {
      clearTimeout(hashTimer);
      hashTimer = setTimeout(() => {
        const next = 'px=' + panX + '&py=' + panY + '&s=' + scale;
        if (location.hash.slice(1) !== next) {
          history.replaceState(null, '', '#' + next);
        }
      }, 120);
    }

    function preserveViewOnResize() {
      const cx = viewport.clientWidth / 2;
      const cy = viewport.clientHeight / 2;
      const wx = (cx - panX) / scale;
      const wz = (cy - panY) / scale;
      panX = cx - wx * scale;
      panY = cy - wz * scale;
      applyTransform();
    }

    function centerOnTile(t, zoom) {
      const cx = (t.wx + BLOCKS_PER_TILE / 2) * PX_PER_BLOCK;
      const cz = (t.wz + BLOCKS_PER_TILE / 2) * PX_PER_BLOCK;
      scale = zoom;
      panX = viewport.clientWidth / 2 - cx * scale;
      panY = viewport.clientHeight / 2 - cz * scale;
      applyTransform();
    }

    function screenToBlock(clientX, clientY) {
      const rect = viewport.getBoundingClientRect();
      const sx = clientX - rect.left;
      const sy = clientY - rect.top;
      const wx = (sx - panX) / scale;
      const wz = (sy - panY) / scale;
      return {
        x: Math.floor(wx / PX_PER_BLOCK),
        z: Math.floor(wz / PX_PER_BLOCK),
      };
    }

    function fitAll() {
      let minX = Infinity, minZ = Infinity, maxX = -Infinity, maxZ = -Infinity;
      for (const t of TILES) {
        minX = Math.min(minX, t.wx);
        minZ = Math.min(minZ, t.wz);
        maxX = Math.max(maxX, t.wx + BLOCKS_PER_TILE);
        maxZ = Math.max(maxZ, t.wz + BLOCKS_PER_TILE);
      }
      const wPx = (maxX - minX) * PX_PER_BLOCK;
      const hPx = (maxZ - minZ) * PX_PER_BLOCK;
      const pad = 32;
      const zx = (viewport.clientWidth - pad) / wPx;
      const zz = (viewport.clientHeight - pad) / hPx;
      scale = Math.min(zx, zz, 4);
      const cx = (minX + maxX) / 2 * PX_PER_BLOCK;
      const cz = (minZ + maxZ) / 2 * PX_PER_BLOCK;
      panX = viewport.clientWidth / 2 - cx * scale;
      panY = viewport.clientHeight / 2 - cz * scale;
      applyTransform();
    }

    viewport.addEventListener('pointerdown', (e) => {
      if (e.button !== 0) return;
      dragging = true;
      lastX = e.clientX;
      lastY = e.clientY;
      viewport.classList.add('dragging');
      viewport.setPointerCapture(e.pointerId);
    });
    viewport.addEventListener('pointermove', (e) => {
      const b = screenToBlock(e.clientX, e.clientY);
      hud.textContent = 'block ' + b.x + ' , ' + b.z + '  ·  zoom ' + scale.toFixed(2) + '×';
      if (!dragging) return;
      panX += e.clientX - lastX;
      panY += e.clientY - lastY;
      lastX = e.clientX;
      lastY = e.clientY;
      applyTransform();
    });
    viewport.addEventListener('pointerup', (e) => {
      dragging = false;
      viewport.classList.remove('dragging');
      try { viewport.releasePointerCapture(e.pointerId); } catch (_) {}
    });
    viewport.addEventListener('pointercancel', () => {
      dragging = false;
      viewport.classList.remove('dragging');
    });

    viewport.addEventListener('wheel', (e) => {
      e.preventDefault();
      const rect = viewport.getBoundingClientRect();
      const sx = e.clientX - rect.left;
      const sy = e.clientY - rect.top;
      const factor = e.deltaY < 0 ? 1.12 : 1 / 1.12;
      const next = Math.min(64, Math.max(0.05, scale * factor));
      const wx = (sx - panX) / scale;
      const wz = (sy - panY) / scale;
      scale = next;
      panX = sx - wx * scale;
      panY = sy - wz * scale;
      applyTransform();
    }, { passive: false });

    window.addEventListener('resize', preserveViewOnResize);

    const fromHash = parseHash();
    if (fromHash) {
      panX = fromHash.panX;
      panY = fromHash.panY;
      scale = fromHash.scale;
      applyTransform();
    } else {
      centerOnTile(INITIAL, 1.25);
    }
  </script>
</body>
</html>
`;

const outPath = path.join(dir, 'index.html');
fs.writeFileSync(outPath, html);
console.error(`wrote ${outPath} (${tiles.length} tile(s), focus x${initial.wx}_z${initial.wz})`);

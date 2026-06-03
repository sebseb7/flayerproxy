import fs from 'node:fs';
import path from 'node:path';
import { createRequire } from 'node:module';
const require = createRequire(import.meta.url);
const lc = require('../libchunk/js/index.js');

const CHUNKS_DIR = path.resolve('chunks');

/** Java-style n % 16 (truncating toward zero). */
function mod16(n) {
  return n - Math.floor(n / 16) * 16;
}

/** l1 = n - (n % 16) */
function chunkL1(n) {
  return n - mod16(n);
}

/** l2 = l1 - (l1 % 16) */
function chunkL2(n) {
  const l1 = chunkL1(n);
  return l1 - mod16(l1);
}

/**
 * Chunk column location from map_chunk payload (payload only, no packet id).
 * Uses libchunk peek (8 bytes) — does not parse heightmaps/sections.
 * @param {Buffer} payload
 * @returns {{ chunkX: number, chunkZ: number, l1x: number, l2x: number, l1z: number, l2z: number, blockX: number, blockZ: number } | null}
 */
export function getLocationFromChunkPayload(payload) {
  const r = lc.peekMapChunkCoords(payload);
  if (!r?.ok) return null;
  const chunkX = r.x;
  const chunkZ = r.z;
  return {
    chunkX,
    chunkZ,
    l1x: chunkL1(chunkX),
    l2x: chunkL2(chunkX),
    l1z: chunkL1(chunkZ),
    l2z: chunkL2(chunkZ),
    blockX: chunkX * 16,
    blockZ: chunkZ * 16,
  };
}

/**
 * Save raw map_chunk payload under
 * chunks/{l1x}/{l2x}/{l1z}/{l2z}/{chunkX}_{chunkZ}_map_chunk.wire
 * @returns {string} absolute path written
 */
export function saveMapChunkWire(loc, payload) {
  const dir = path.join(
    CHUNKS_DIR,
    String(loc.l1x),
    String(loc.l2x),
    String(loc.l1z),
    String(loc.l2z),
  );
  const file = path.join(dir, `${loc.chunkX}_${loc.chunkZ}_map_chunk.wire`);
  fs.mkdirSync(dir, { recursive: true });
  fs.writeFileSync(file, payload);
  return file;
}

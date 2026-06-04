import { createRequire } from 'node:module';

const require = createRequire(import.meta.url);

/** @type {string[] | null} */
let byRegistryIndex = null;

function load() {
  if (byRegistryIndex) return;
  /** @type {{ id: number, name: string }[]} */
  const sounds = require('minecraft-data/minecraft-data/data/pc/1.21.9/sounds.json');
  let max = 0;
  for (const s of sounds) {
    if (s.id > max) max = s.id;
  }
  byRegistryIndex = new Array(max);
  for (const s of sounds) {
    byRegistryIndex[s.id - 1] = s.name;
  }
}

/** @param {number} registryIndex 0-based id from wire (libchunk sound=#N) */
export function soundNameByRegistryIndex(registryIndex) {
  load();
  return byRegistryIndex[registryIndex] ?? null;
}

/** Replace sound=#123 with sound=entity.foo from minecraft-data. */
export function resolveSoundRefs(text) {
  if (!text || !text.includes('sound=#')) return text;
  return text.replace(/sound=#(\d+)/g, (match, raw) => {
    const name = soundNameByRegistryIndex(Number(raw));
    return name ? `sound=${name}` : match;
  });
}

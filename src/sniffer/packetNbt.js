'use strict';

const zlib = require('zlib');
const nbt = require('prismarine-nbt');

/**
 * @param {Buffer|Uint8Array|number[]|null|undefined} value
 * @returns {Buffer|null}
 */
function asBuffer(value) {
  if (value == null) return null;
  if (Buffer.isBuffer(value)) return value;
  if (value instanceof Uint8Array || Array.isArray(value)) return Buffer.from(value);
  return null;
}

/**
 * @param {Buffer|Uint8Array|object|null|undefined} nbtData
 * @returns {Record<string, object>|null}
 */
function parseBlockEntityNbtPayload(nbtData) {
  if (nbtData == null) return null;
  if (nbtData.type === 'compound') {
    if (!nbtData.value || !Object.keys(nbtData.value).length) return null;
    return { ...nbtData.value };
  }
  const buf = asBuffer(nbtData);
  if (!buf?.length) return null;
  const attempts = [buf];
  try {
    attempts.push(zlib.gunzipSync(buf));
  } catch {
    /* not gzipped */
  }
  for (const data of attempts) {
    try {
      const root = nbt.parseUncompressed(data);
      if (root?.type === 'compound' && root.value) return { ...root.value };
    } catch {
      /* try next */
    }
  }
  return null;
}

module.exports = { asBuffer, parseBlockEntityNbtPayload };

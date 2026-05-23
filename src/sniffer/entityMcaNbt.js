'use strict';

const fs = require('fs');
const zlib = require('zlib');

const SECTOR_BYTES = 4096;

const TAG = {
  END: 0,
  BYTE: 1,
  SHORT: 2,
  INT: 3,
  LONG: 4,
  FLOAT: 5,
  DOUBLE: 6,
  BYTE_ARRAY: 7,
  STRING: 8,
  LIST: 9,
  COMPOUND: 10,
  INT_ARRAY: 11,
  LONG_ARRAY: 12,
};

const TAG_NAME = Object.fromEntries(
  Object.entries(TAG).map(([k, v]) => [v, k.toLowerCase()]),
);

/**
 * @typedef {object} NbtTag
 * @property {string} type
 * @property {string} [name]
 * @property {*} value
 */

class NbtReader {
  /** @param {Buffer} buf */
  constructor(buf) {
    this.buf = buf;
    this.off = 0;
  }

  u8() {
    return this.buf.readUInt8(this.off++);
  }

  i8() {
    return this.buf.readInt8(this.off++);
  }

  i16() {
    const v = this.buf.readInt16BE(this.off);
    this.off += 2;
    return v;
  }

  i32() {
    const v = this.buf.readInt32BE(this.off);
    this.off += 4;
    return v;
  }

  i64() {
    const v = this.buf.readBigInt64BE(this.off);
    this.off += 8;
    return v;
  }

  f32() {
    const v = this.buf.readFloatBE(this.off);
    this.off += 4;
    return v;
  }

  f64() {
    const v = this.buf.readDoubleBE(this.off);
    this.off += 8;
    return v;
  }

  string() {
    const len = this.i16();
    const s = this.buf.toString('utf8', this.off, this.off + len);
    this.off += len;
    return s;
  }

  /** @returns {NbtTag} */
  readTag() {
    const typeId = this.u8();
    if (typeId === TAG.END) {
      return { type: 'end' };
    }
    const name = this.string();
    const value = this.readPayload(typeId);
    return { type: TAG_NAME[typeId] ?? String(typeId), name, value };
  }

  /** @param {number} typeId */
  readPayload(typeId) {
    switch (typeId) {
      case TAG.BYTE:
        return this.i8();
      case TAG.SHORT:
        return this.i16();
      case TAG.INT:
        return this.i32();
      case TAG.LONG:
        return this.i64().toString();
      case TAG.FLOAT:
        return this.f32();
      case TAG.DOUBLE:
        return this.f64();
      case TAG.BYTE_ARRAY: {
        const len = this.i32();
        return this.buf.subarray(this.off, (this.off += len));
      }
      case TAG.STRING:
        return this.string();
      case TAG.LIST:
        return this.readList();
      case TAG.COMPOUND:
        return this.readCompoundFields();
      case TAG.INT_ARRAY: {
        const len = this.i32();
        const arr = [];
        for (let i = 0; i < len; i++) arr.push(this.i32());
        return arr;
      }
      case TAG.LONG_ARRAY: {
        const len = this.i32();
        const arr = [];
        for (let i = 0; i < len; i++) arr.push(this.i64().toString());
        return arr;
      }
      default:
        throw new Error(`Unknown NBT type ${typeId} at offset ${this.off - 1}`);
    }
  }

  readList() {
    const elementType = this.u8();
    const len = this.i32();
    const elementTypeName = TAG_NAME[elementType] ?? String(elementType);
    const items = [];
    for (let i = 0; i < len; i++) {
      if (elementType === TAG.COMPOUND) {
        items.push(this.readCompoundFields());
      } else {
        items.push(this.readPayload(elementType));
      }
    }
    return { elementType: elementTypeName, length: len, items };
  }

  /** Unnamed compound body (list elements or root). */
  readCompoundFields() {
    /** @type {Record<string, *>} */
    const fields = {};
    while (this.off < this.buf.length) {
      const typeId = this.u8();
      if (typeId === TAG.END) break;
      const name = this.string();
      fields[name] = this.readPayload(typeId);
    }
    return fields;
  }

  /** @returns {{ name: string, value: Record<string, *> }} */
  readRootCompound() {
    const typeId = this.u8();
    if (typeId !== TAG.COMPOUND) {
      throw new Error(`Root must be compound, got ${typeId}`);
    }
    const name = this.string();
    return { name, value: this.readCompoundFields() };
  }
}

/**
 * @param {Buffer} regionBuf
 * @param {number} chunkX
 * @param {number} chunkZ
 * @returns {{ compression: number, payload: Buffer }|null}
 */
function readRegionChunkPayload(regionBuf, chunkX, chunkZ) {
  const localX = chunkX & 31;
  const localZ = chunkZ & 31;
  const index = localX + localZ * 32;
  const entryOff = index * 4;
  const sectorOffset = regionBuf.readUInt32BE(entryOff);
  if (!sectorOffset) return null;

  const sectorIndex = sectorOffset >>> 8;
  const sectorCount = sectorOffset & 0xff;
  const byteOff = sectorIndex * SECTOR_BYTES;
  // Length field = compressed bytes + 1 (compression type byte).
  const length = regionBuf.readUInt32BE(byteOff);
  const compression = regionBuf.readUInt8(byteOff + 4);
  const compressed = regionBuf.subarray(byteOff + 5, byteOff + 5 + length - 1);

  let payload;
  if (compression === 1) payload = zlib.gunzipSync(compressed);
  else if (compression === 2) payload = zlib.inflateSync(compressed);
  else payload = Buffer.from(compressed);

  return { compression, payload, sectorCount };
}

/**
 * @param {string} regionPath
 * @param {number} chunkX
 * @param {number} chunkZ
 */
function readEntityChunkNbt(regionPath, chunkX, chunkZ) {
  const regionBuf = fs.readFileSync(regionPath);
  const chunk = readRegionChunkPayload(regionBuf, chunkX, chunkZ);
  if (!chunk) return null;

  const reader = new NbtReader(chunk.payload);
  const root = reader.readRootCompound();
  return {
    chunkX,
    chunkZ,
    compression: chunk.compression,
    root: root.value,
  };
}

/**
 * Turn parsed NBT values into plain JSON-safe objects.
 * @param {*} value
 * @returns {*}
 */
function toPlain(value) {
  if (value == null) return value;
  if (Buffer.isBuffer(value)) return { _byteArray: value.length };
  if (typeof value === 'bigint') return value.toString();
  if (Array.isArray(value) && !(value.elementType != null)) {
    return value.map(toPlain);
  }
  if (typeof value === 'object' && value.elementType != null) {
    if (value.elementType === 'compound') {
      return value.items.map((item) => toPlain(item));
    }
    return value.items;
  }
  if (typeof value === 'object' && !Array.isArray(value)) {
    /** @type {Record<string, *>} */
    const out = {};
    for (const [k, v] of Object.entries(value)) {
      out[k] = toPlain(v);
    }
    return out;
  }
  return value;
}

/**
 * @param {Record<string, *>} root
 */
function parseEntityChunkColumn(root) {
  const entities = root.Entities?.items ?? [];
  return {
    position: root.Position ?? null,
    dataVersion: root.DataVersion ?? null,
    entities: entities.map((fields) => toPlain(fields)),
  };
}

/**
 * @param {string} regionPath
 * @param {number} chunkX
 * @param {number} chunkZ
 */
function readEntityChunkPlain(regionPath, chunkX, chunkZ) {
  const parsed = readEntityChunkNbt(regionPath, chunkX, chunkZ);
  if (!parsed) return null;
  return {
    chunkX,
    chunkZ,
    compression: parsed.compression,
    ...parseEntityChunkColumn(parsed.root),
  };
}

module.exports = {
  TAG,
  NbtReader,
  readRegionChunkPayload,
  readEntityChunkNbt,
  readEntityChunkPlain,
  parseEntityChunkColumn,
  toPlain,
};

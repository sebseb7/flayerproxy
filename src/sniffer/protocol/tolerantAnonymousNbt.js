'use strict';

/**
 * Java Edition item NBT reader/writer (replaces prismarine anonymousNbt on the sniffer legs).
 * Handles compound roots, single root tags (e.g. custom_name string), Paper bees prefix, and
 * preserves original wire in `_wire` for byte-identical relay.
 */

const TAG_END = 0;
const TAG_BYTE = 1;
const TAG_SHORT = 2;
const TAG_INT = 3;
const TAG_LONG = 4;
const TAG_FLOAT = 5;
const TAG_DOUBLE = 6;
const TAG_BYTE_ARRAY = 7;
const TAG_STRING = 8;
const TAG_LIST = 9;
const TAG_COMPOUND = 10;
const TAG_INT_ARRAY = 11;
const TAG_LONG_ARRAY = 12;

const TAG_BY_NAME = {
  end: TAG_END,
  byte: TAG_BYTE,
  short: TAG_SHORT,
  int: TAG_INT,
  long: TAG_LONG,
  float: TAG_FLOAT,
  double: TAG_DOUBLE,
  byteArray: TAG_BYTE_ARRAY,
  string: TAG_STRING,
  list: TAG_LIST,
  compound: TAG_COMPOUND,
  intArray: TAG_INT_ARRAY,
  longArray: TAG_LONG_ARRAY,
};

const TAG_NAME = Object.fromEntries(
  Object.entries(TAG_BY_NAME).map(([name, id]) => [id, name]),
);

function normalizeTagType(tagType) {
  if (typeof tagType === 'string') {
    if (TAG_BY_NAME[tagType] !== undefined) return TAG_BY_NAME[tagType];
    const n = Number(tagType);
    if (!Number.isNaN(n)) return n;
  }
  return tagType;
}

function toProtodefType(tagType) {
  return TAG_NAME[normalizeTagType(tagType)] ?? 'byte';
}

function readU16BE(buffer, offset) {
  return buffer.readUInt16BE(offset);
}

function readI32BE(buffer, offset) {
  return buffer.readInt32BE(offset);
}

function readStringBE(buffer, offset) {
  const len = readU16BE(buffer, offset);
  const start = offset + 2;
  return {
    value: buffer.toString('utf8', start, start + len),
    size: 2 + len,
  };
}

function readPayload(buffer, offset, tagType) {
  const t = normalizeTagType(tagType);
  switch (t) {
    case TAG_BYTE:
      return { value: buffer.readInt8(offset), size: 1 };
    case TAG_SHORT:
      return { value: buffer.readInt16BE(offset), size: 2 };
    case TAG_INT:
      return { value: readI32BE(buffer, offset), size: 4 };
    case TAG_LONG:
      return { value: buffer.readBigInt64BE(offset), size: 8 };
    case TAG_FLOAT:
      return { value: buffer.readFloatBE(offset), size: 4 };
    case TAG_DOUBLE:
      return { value: buffer.readDoubleBE(offset), size: 8 };
    case TAG_BYTE_ARRAY: {
      const len = readI32BE(buffer, offset);
      const start = offset + 4;
      return {
        value: buffer.subarray(start, start + len),
        size: 4 + len,
      };
    }
    case TAG_STRING:
      return readStringBE(buffer, offset);
    case TAG_LIST: {
      const elemType = buffer.readUInt8(offset);
      const count = readI32BE(buffer, offset + 1);
      let o = offset + 5;
      const items = [];
      for (let i = 0; i < count; i++) {
        const elem = readPayload(buffer, o, elemType);
        items.push(elem.value);
        o += elem.size;
      }
      return {
        value: { type: elemType, value: items },
        size: o - offset,
      };
    }
    case TAG_COMPOUND:
      return readCompoundPayload(buffer, offset);
    case TAG_INT_ARRAY: {
      const count = readI32BE(buffer, offset);
      const start = offset + 4;
      const values = [];
      for (let i = 0; i < count; i++) {
        values.push(readI32BE(buffer, start + i * 4));
      }
      return { value: values, size: 4 + count * 4 };
    }
    case TAG_LONG_ARRAY: {
      const count = readI32BE(buffer, offset);
      const start = offset + 4;
      const values = [];
      for (let i = 0; i < count; i++) {
        values.push(buffer.readBigInt64BE(start + i * 8));
      }
      return { value: values, size: 4 + count * 8 };
    }
    default:
      throw new Error(`unsupported item NBT tag type: ${tagType}`);
  }
}

function readCompoundPayload(buffer, offset) {
  let o = offset;
  const value = {};
  while (true) {
    const tagType = buffer.readUInt8(o);
    o += 1;
    if (tagType === TAG_END) {
      return { value, size: o - offset };
    }
    const name = readStringBE(buffer, o);
    o += name.size;
    const payload = readPayload(buffer, o, tagType);
    o += payload.size;
    value[name.value] = { type: toProtodefType(tagType), value: payload.value };
  }
}

/** @returns {{ value: object, size: number }} */
function readAnonymousNbt(buffer, offset) {
  const start = offset;
  let pos = offset;
  if (
    buffer[pos] !== TAG_COMPOUND &&
    buffer[pos] > 0 &&
    buffer[pos] < 16 &&
    buffer[pos + 1] === TAG_COMPOUND
  ) {
    pos += 1;
  }

  const rootType = buffer.readUInt8(pos);

  if (rootType === TAG_COMPOUND) {
    const payload = readCompoundPayload(buffer, pos + 1);
    const end = pos + 1 + payload.size;
    return {
      value: {
        type: 'compound',
        value: payload.value,
        _wire: buffer.subarray(start, end),
      },
      size: end - start,
    };
  }

  if (rootType === TAG_END || rootType > TAG_LONG_ARRAY) {
    throw new Error(`invalid item NBT root tag: ${rootType}`);
  }

  // custom_name / item_name etc.: single anonymous root tag (often STRING = 8).
  const payload = readPayload(buffer, pos + 1, rootType);
  const end = pos + 1 + payload.size;
  return {
    value: {
      type: toProtodefType(rootType),
      value: payload.value,
      _wire: buffer.subarray(start, end),
    },
    size: end - start,
  };
}

function writeStringBE(value, buffer, offset) {
  const bytes = Buffer.from(String(value), 'utf8');
  buffer.writeUInt16BE(bytes.length, offset);
  bytes.copy(buffer, offset + 2);
  return offset + 2 + bytes.length;
}

function writePayload(tagType, value, buffer, offset) {
  const t = normalizeTagType(tagType);
  switch (t) {
    case TAG_BYTE:
      buffer.writeInt8(value, offset);
      return offset + 1;
    case TAG_SHORT:
      buffer.writeInt16BE(value, offset);
      return offset + 2;
    case TAG_INT:
      buffer.writeInt32BE(value, offset);
      return offset + 4;
    case TAG_LONG:
      buffer.writeBigInt64BE(typeof value === 'bigint' ? value : BigInt(value), offset);
      return offset + 8;
    case TAG_FLOAT:
      buffer.writeFloatBE(value, offset);
      return offset + 4;
    case TAG_DOUBLE:
      buffer.writeDoubleBE(value, offset);
      return offset + 8;
    case TAG_BYTE_ARRAY:
      buffer.writeInt32BE(value.length, offset);
      value.copy(buffer, offset + 4);
      return offset + 4 + value.length;
    case TAG_STRING:
      return writeStringBE(value, buffer, offset);
    case TAG_LIST: {
      let o = offset;
      const elemType = normalizeTagType(value.type);
      buffer.writeUInt8(elemType, o);
      o += 1;
      buffer.writeInt32BE(value.value.length, o);
      o += 4;
      for (const item of value.value) {
        o = writePayload(elemType, item, buffer, o);
      }
      return o;
    }
    case TAG_COMPOUND:
      return writeCompoundPayload(value, buffer, offset);
    case TAG_INT_ARRAY: {
      buffer.writeInt32BE(value.length, offset);
      let o = offset + 4;
      for (const n of value) {
        buffer.writeInt32BE(n, o);
        o += 4;
      }
      return o;
    }
    case TAG_LONG_ARRAY: {
      buffer.writeInt32BE(value.length, offset);
      let o = offset + 4;
      for (const n of value) {
        buffer.writeBigInt64BE(typeof n === 'bigint' ? n : BigInt(n), o);
        o += 8;
      }
      return o;
    }
    default:
      throw new Error(`unsupported item NBT tag type: ${tagType}`);
  }
}

function writeCompoundPayload(value, buffer, offset) {
  let o = offset;
  for (const [name, entry] of Object.entries(value)) {
    const tagType = normalizeTagType(entry.type);
    o = writeNamedTag(tagType, name, entry.value, buffer, o);
  }
  buffer.writeUInt8(TAG_END, o);
  return o + 1;
}

function writeNamedTag(tagType, name, value, buffer, offset) {
  let o = offset;
  buffer.writeUInt8(tagType, o);
  o += 1;
  o = writeStringBE(name, buffer, o);
  o = writePayload(tagType, value, buffer, o);
  return o;
}

function sizeOfAnonymousNbt(value) {
  if (value?._wire) return value._wire.length;
  const rootType = normalizeTagType(value.type);
  if (rootType === TAG_COMPOUND) {
    return 1 + sizeOfCompoundPayload(value.value);
  }
  return 1 + sizeOfPayload(rootType, value.value);
}

function sizeOfCompoundPayload(value) {
  let size = 1;
  for (const [name, entry] of Object.entries(value)) {
    size +=
      1 +
      2 +
      Buffer.byteLength(name, 'utf8') +
      sizeOfPayload(entry.type, entry.value);
  }
  return size;
}

function sizeOfPayload(tagType, value) {
  const t = normalizeTagType(tagType);
  switch (t) {
    case TAG_BYTE:
      return 1;
    case TAG_SHORT:
      return 2;
    case TAG_INT:
      return 4;
    case TAG_LONG:
      return 8;
    case TAG_FLOAT:
      return 4;
    case TAG_DOUBLE:
      return 8;
    case TAG_BYTE_ARRAY:
      return 4 + value.length;
    case TAG_STRING:
      return 2 + Buffer.byteLength(String(value), 'utf8');
    case TAG_LIST:
      return (
        1 +
        4 +
        value.value.reduce(
          (s, item) => s + sizeOfPayload(value.type, item),
          0,
        )
      );
    case TAG_COMPOUND:
      return sizeOfCompoundPayload(value);
    case TAG_INT_ARRAY:
      return 4 + value.length * 4;
    case TAG_LONG_ARRAY:
      return 4 + value.length * 8;
    default:
      throw new Error(`unsupported item NBT tag type: ${tagType}`);
  }
}

function writeAnonymousNbt(value, buffer, offset) {
  if (value?._wire) {
    value._wire.copy(buffer, offset);
    return offset + value._wire.length;
  }
  const rootType = normalizeTagType(value.type);
  let o = offset;
  buffer.writeUInt8(rootType, o);
  o += 1;
  if (rootType === TAG_COMPOUND) {
    o = writeCompoundPayload(value.value, buffer, o);
  } else {
    o = writePayload(rootType, value.value, buffer, o);
  }
  return o;
}

module.exports = {
  readAnonymousNbt,
  writeAnonymousNbt,
  sizeOfAnonymousNbt,
};

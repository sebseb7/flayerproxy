'use strict';

/** Compact JSON for trace lines and inline summaries. */
function summarizePacket(name, data) {
  return serializePacket(data, replacerCompact);
}

/** Full protocol params — no truncation (decoded/ dumps and includePayload data). */
function serializePacketFull(data) {
  return serializePacket(data, replacerFull);
}

function serializePacket(data, replacer) {
  if (data == null) return data;
  if (Buffer.isBuffer(data)) {
    return bufferToJson(data);
  }
  if (typeof data !== 'object') return data;

  try {
    return JSON.parse(JSON.stringify(data, replacer));
  } catch {
    return { _type: 'Unserializable' };
  }
}

function replacerFull(_key, value) {
  if (Buffer.isBuffer(value)) return bufferToJson(value);
  if (value instanceof Uint8Array) return typedArrayToJson(value);
  if (isBufferJson(value)) return bufferToJson(Buffer.from(value.data));
  return value;
}

function replacerCompact(_key, value) {
  if (Buffer.isBuffer(value)) {
    return { _type: 'Buffer', length: value.length };
  }
  if (isBufferJson(value)) {
    return { _type: 'Buffer', length: value.data.length };
  }
  if (typeof value === 'string' && value.length > 512) {
    return `${value.slice(0, 512)}…(${value.length} chars)`;
  }
  if (Array.isArray(value) && value.length > 32) {
    return { _type: 'Array', length: value.length, sample: value.slice(0, 3) };
  }
  return value;
}

function bufferToJson(buf) {
  return { _type: 'Buffer', encoding: 'base64', length: buf.length, data: buf.toString('base64') };
}

function typedArrayToJson(arr) {
  return {
    _type: arr.constructor.name,
    encoding: 'base64',
    length: arr.length,
    data: Buffer.from(arr.buffer, arr.byteOffset, arr.byteLength).toString('base64'),
  };
}

function isBufferJson(value) {
  return (
    value &&
    typeof value === 'object' &&
    value.type === 'Buffer' &&
    Array.isArray(value.data)
  );
}

module.exports = { summarizePacket, serializePacketFull, replacerCompact, replacerFull };

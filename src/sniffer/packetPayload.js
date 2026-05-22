'use strict';

const { serializePacketFull } = require('./packetSummarize');

/** Inline wire in trace/jsonl when at or below this size (bytes). */
const INLINE_WIRE_MAX = 2048;

/**
 * Full parsed packet + wire (same shape as decoded/*.json).
 * @param {object} data
 * @param {Buffer} [rawBuffer]
 * @param {string} [decodedFile]
 */
function buildFullPacketPayload(data, rawBuffer, decodedFile) {
  const payload = {
    params: serializePacketFull(data),
  };
  if (rawBuffer?.length) {
    if (rawBuffer.length <= INLINE_WIRE_MAX) {
      payload.wire = {
        encoding: 'base64',
        length: rawBuffer.length,
        data: rawBuffer.toString('base64'),
      };
    } else {
      payload.wireBytes = rawBuffer.length;
    }
  }
  if (decodedFile) payload.decodedFile = decodedFile;
  return payload;
}

/**
 * @param {object} payload - from buildFullPacketPayload
 * @param {number} [maxLen]
 */
function formatTracePayload(payload, maxLen = 8192) {
  if (payload == null) return null;
  try {
    const s = typeof payload === 'string' ? payload : JSON.stringify(payload);
    if (!s || s === '{}') return '{}';
    if (s.length <= maxLen) return s;
    if (payload.decodedFile) {
      const compact = {
        decodedFile: payload.decodedFile,
        wireBytes: payload.wireBytes ?? payload.wire?.length ?? null,
        note: 'full params and wire in decoded file',
      };
      if (payload.params && typeof payload.params === 'object') {
        const p = payload.params;
        if (p.x != null && p.z != null) {
          compact.x = p.x;
          compact.z = p.z;
        }
        if (p.id != null) compact.id = p.id;
        if (p.location) compact.location = p.location;
        if (p.chunkCoordinates) compact.chunkCoordinates = p.chunkCoordinates;
        if (p.records) compact.records = p.records;
      }
      const compactStr = JSON.stringify(compact);
      return compactStr.length <= maxLen ? compactStr : `${compactStr.slice(0, maxLen - 1)}…`;
    }
    return `${s.slice(0, maxLen)}…`;
  } catch {
    return '?';
  }
}

module.exports = { buildFullPacketPayload, formatTracePayload, INLINE_WIRE_MAX };

'use strict';

const { serializePacketFull } = require('./packetSummarize');

/**
 * Build a JSON-serializable decode of a sniffer packet for logs/sniffer/chunks/.../decoded/.
 * Full protocol params (buffers as base64, arrays untruncated).
 *
 * @param {string} name
 * @param {object} data
 * @param {Buffer} [rawBuffer]
 */
function buildDecodedPacketRecord(name, data, rawBuffer, nameInfo = {}) {
  const record = {
    name,
    rawBytes: rawBuffer?.length ?? null,
    params: serializePacketFull(data),
  };
  if (nameInfo.packetId != null) record.packetId = nameInfo.packetId;
  if (nameInfo.unknown) record.unknown = true;
  if (nameInfo.displayName && nameInfo.displayName !== name) record.displayName = nameInfo.displayName;
  if (nameInfo.note) record.note = nameInfo.note;
  if (rawBuffer?.length) {
    record.wire = {
      _type: 'Buffer',
      encoding: 'base64',
      length: rawBuffer.length,
      data: rawBuffer.toString('base64'),
    };
  }
  return record;
}

function buildDecodedDumpFileName(seq, nameInfo, data) {
  let base = `${String(seq).padStart(6, '0')}-${nameInfo.name}`;
  if (nameInfo.packetId != null) base += `-id${nameInfo.packetId}`;
  if (data && typeof data === 'object') {
    if (data.x != null && data.z != null) base += `-${data.x}-${data.z}`;
  }
  return `${base}.json`;
}

module.exports = { buildDecodedPacketRecord, buildDecodedDumpFileName };

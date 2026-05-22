const { createLogger } = require('../utils/logger');

const traceLog = createLogger('PktTrace');

/**
 * Human-readable one-line trace (always short — no JSON spill).
 * @param {object} e
 */
function formatTraceLine(e) {
  const seq = e.seq != null ? String(e.seq).padStart(5, ' ') : '     ';
  const t = e.t ? e.t.slice(11, 23) : '??:??:??.???';
  const dir = e.dir || '??';
  const state = e.state || '?';
  const name = e.name || '?';
  const bytes = e.rawBytes != null ? `${e.rawBytes}b` : '';

  if (e.event === 'relay') {
    const parts = [
      `#${seq}`,
      t,
      'RELAY',
      e.bridge || '?',
      dir,
      `${state}.${name}`,
      bytes,
      e.method,
      e.payload,
    ].filter(Boolean);
    return parts.join(' ');
  }

  const leg = (e.leg || '?').toUpperCase().padEnd(7, ' ');
  const parts = [`#${seq}`, t, leg, dir, `${state}.${name}`, bytes].filter(Boolean);

  if (e.event && e.event !== 'rx') parts.push(`event=${e.event}`);
  if (e.action) parts.push(`action=${e.action}`);
  if (e.bridge) parts.push(`bridge=${e.bridge}`);
  if (e.method) parts.push(`method=${e.method}`);
  if (e.note) parts.push(`note=${e.note}`);
  if (e.payload) parts.push(e.payload);

  return parts.join(' ');
}

const { formatTracePayload: formatFullTracePayload } = require('./packetPayload');

/** @deprecated use packetPayload.formatTracePayload */
function formatTracePayload(summary, maxLen) {
  return formatFullTracePayload(summary, maxLen);
}

/**
 * @param {import('./PacketLog').PacketLog} packetLog
 * @param {object} e
 */
function emitTrace(packetLog, e) {
  packetLog._recordTrace(e);
}

/**
 * Observed (decoded) on a leg.
 * @param {import('./PacketLog').PacketLog} packetLog
 * @param {'java'|'backend'} leg
 * @param {'C2S'|'S2C'} dir
 * @param {object} meta
 * @param {Buffer} [buffer]
 * @param {object} [extra]
 */
function traceRx(packetLog, leg, dir, meta, buffer, extra = {}) {
  emitTrace(packetLog, {
    type: 'trace',
    event: 'rx',
    leg,
    dir,
    state: meta.state,
    name: meta.name,
    rawBytes: buffer?.length,
    action: extra.action ?? extra.forwarded ?? null,
    clientState: extra.clientState,
    upstreamState: extra.upstreamState,
    note: extra.note,
  });
}

/**
 * Written to a leg or cross-leg relay.
 */
function traceTx(packetLog, leg, dir, meta, buffer, extra = {}) {
  emitTrace(packetLog, {
    type: 'trace',
    event: 'tx',
    leg,
    dir,
    state: meta.state,
    name: meta.name,
    rawBytes: buffer?.length,
    action: extra.action ?? 'relay',
    bridge: extra.bridge,
    method: extra.method,
    note: extra.note,
    payload: extra.payload ?? null,
  });
}

/**
 * Single line for a packet relayed across legs (replaces separate rx + tx lines).
 */
function traceRelay(packetLog, { bridge, dir, meta, data, buffer, method, action }) {
  const decodedFile = packetLog.consumePendingDecodedFile?.();
  const payload = packetLog.buildFullPacketPayload(data, buffer, decodedFile);
  const traceName = packetLog.consumePendingTracePacketName?.() ?? meta.name;
  emitTrace(packetLog, {
    type: 'trace',
    event: 'relay',
    bridge,
    dir,
    state: meta.state,
    name: traceName,
    rawBytes: buffer?.length,
    method,
    action: action || 'relay',
    payload: formatFullTracePayload(payload, packetLog.tracePayloadMaxLen),
  });
}

/**
 * Cross-leg bridge decision without a wire write (skip, hold, queue).
 */
function traceBridge(packetLog, meta, buffer, extra = {}) {
  emitTrace(packetLog, {
    type: 'trace',
    event: extra.event || 'bridge',
    leg: extra.leg || 'bridge',
    dir: meta ? (extra.dir || 'C2S') : '—',
    state: meta?.state,
    name: meta?.name,
    rawBytes: buffer?.length,
    action: extra.action,
    bridge: extra.bridge,
    note: extra.note,
  });
}

function traceMeta(packetLog, record) {
  emitTrace(packetLog, {
    type: 'trace',
    event: 'meta',
    leg: 'session',
    dir: '—',
    state: record.type,
    name: record.type,
    action: record.reason ?? record.mode ?? null,
    note: summarizeMeta(record),
  });
  if (packetLog.consolePacketLog) {
    traceLog.info(`[meta] ${record.type}${record.reason ? ` ${record.reason}` : ''}${record.username ? ` ${record.username}` : ''}`);
  }
}

function summarizeMeta(record) {
  if (record.type === 'java_crypto_ready') return 'java leg encrypted';
  if (record.type === 'upstream_connect') return record.mode || 'login';
  if (record.type === 'username') return record.username;
  return null;
}

module.exports = {
  formatTraceLine,
  formatTracePayload,
  emitTrace,
  traceRx,
  traceTx,
  traceRelay,
  traceBridge,
  traceMeta,
};

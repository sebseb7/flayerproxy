'use strict';

/**
 * Log minecraft-protocol deserializer failures when the error carries the raw frame.
 *
 * @param {import('./PacketLog').PacketLog | null | undefined} packetLog
 * @param {'C2S'|'S2C'} dir
 * @param {string} state
 * @param {Error} err
 * @param {object} [extra]
 */
function logDeserializerError(packetLog, dir, state, err, extra = {}) {
  if (!packetLog || !err?.buffer?.length) return;
  packetLog.logDeserializerError(dir, state, err, extra);
}

module.exports = { logDeserializerError };

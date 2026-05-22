const { GATE } = require('./mitmGate');

/**
 * Build the per-connection MITM session state bag.
 * @param {object} client - minecraft-protocol client
 * @param {import('./PacketLog').PacketLog} packetLog
 * @returns {object} session state
 */
function createMitmSession(client, packetLog) {
  return {
    client,
    upstream: null,
    bridged: false,
    gate: GATE.LOGIN,
    holdS2C: false,
    pendingS2C: [],
    pendingConfig: [],
    pendingPlay: [],
    waitingJavaCrypto: false,
    javaCryptoStarting: false,
    relayedCompress: false,
    statusPipe: null,
    packetLog,
    username: 'unknown',
    cleaned: false,
  };
}

/**
 * Build the cleanup closure for a MITM session.
 * @param {object} session
 * @param {import('./PacketLog').PacketLog} packetLog
 * @param {{ activeSession: object|null }} proxy - MitmProxy instance (mutated on cleanup)
 * @returns {function(string): void}
 */
function createSessionCleanup(session, packetLog, proxy) {
  return (reason) => {
    if (session.cleaned) return;
    session.cleaned = true;

    if (session.statusPipe) {
      try { session.statusPipe.client.destroy(); } catch (_) {}
      try { session.statusPipe.upstream.destroy(); } catch (_) {}
    }
    if (session.upstream && !session.upstream.ended) {
      try { session.upstream.end(reason); } catch (_) {}
    }
    packetLog.writeMeta({
      type: 'session_stats',
      reason,
      username: session.username,
      bridged: session.bridged,
    });
    packetLog.close(reason);
    if (proxy.activeSession === session) proxy.activeSession = null;
  };
}

module.exports = { createMitmSession, createSessionCleanup };

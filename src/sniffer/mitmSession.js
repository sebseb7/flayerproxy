const { traceBridge, traceRelay } = require('./packetTrace');
const { relayPacket } = require('./mitmRelay');
const { shouldRelayC2SToUpstream } = require('./mitmLoginBridge');

/**
 * @param {object} client - minecraft-protocol server peer (Java)
 * @param {import('./PacketLog').PacketLog} packetLog
 */
function createMitmSession(client, packetLog) {
  return {
    client,
    upstream: null,
    upstreamLink: false,
    statusPipe: null,
    packetLog,
    username: 'unknown',
    cleaned: false,
    holdS2C: false,
    pendingS2C: [],
    pendingConfig: [],
    waitingJavaCrypto: false,
    javaCryptoStarting: false,
    javaLegEncrypted: false,
    upstreamEncryptRequest: null,
    upstreamEncryptDone: false,
    javaCryptoReady: false,
    javaLoginAcknowledged: false,
    javaFinishConfiguration: false,
    c2sQueue: [],
  };
}

function flushC2sQueue(session) {
  if (!session.upstream || !session.upstreamLink) return;
  for (const { meta, data, buffer } of session.c2sQueue) {
    if (!shouldRelayC2SToUpstream(meta, session)) {
      traceBridge(session.packetLog, meta, buffer, {
        action: 'skip_flush',
        bridge: 'java→backend',
        dir: 'C2S',
        note: 'not relayed on queue flush',
      });
      continue;
    }
    try {
      const method = relayPacket(session.upstream, meta, data, buffer);
      traceRelay(session.packetLog, {
        bridge: 'java→backend',
        dir: 'C2S',
        meta,
        data,
        buffer,
        method,
        action: 'flush_queue',
      });
    } catch (err) {
      throw new Error(`C2S ${meta.state}.${meta.name}: ${err.message}`);
    }
  }
  session.c2sQueue.length = 0;
}

/**
 * @param {object} session
 * @param {{ activeSession: object|null }} proxy
 */
function createSessionCleanup(session, proxy) {
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
    const packetLog = session.packetLog;
    if (packetLog) {
      packetLog.writeMeta({
        type: 'session_stats',
        reason,
        username: session.username,
      });
      packetLog.close(reason);
    }
    if (proxy.activeSession === session) proxy.activeSession = null;
  };
}

module.exports = { createMitmSession, createSessionCleanup, flushC2sQueue };

const { traceBridge, traceRelay } = require('./packetTrace');
const { relayPacket } = require('./mitmRelay');
const { shouldRelayC2SToUpstream } = require('./mitmLoginBridge');
const { exportSessionWorld } = require('./levelSaveExporter');
const { createLogger } = require('../utils/logger');

const log = createLogger('Sniffer');

/**
 * @param {object} client - minecraft-protocol server peer (Java)
 * @param {import('./PacketLog').PacketLog} packetLog
 * @param {import('./SnifferWorldCapture').SnifferWorldCapture|null} [worldCapture]
 */
function createMitmSession(client, packetLog, worldCapture = null) {
  return {
    client,
    worldCapture,
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
 * @param {{ activeSession: object|null, config: object }} proxy
 */
function createSessionCleanup(session, proxy) {
  return async (reason) => {
    if (session.cleaned) return;
    session.cleaned = true;

    if (session.statusPipe) {
      try { session.statusPipe.client.destroy(); } catch (_) {}
      try { session.statusPipe.upstream.destroy(); } catch (_) {}
    }
    if (session.upstream && !session.upstream.ended) {
      try { session.upstream.end(reason); } catch (_) {}
    }

    // Release the sniffer slot before the async world write (can take ~1s).
    if (proxy.activeSession === session) proxy.activeSession = null;

    const packetLog = session.packetLog;
    const sniffer = proxy.config?.sniffer ?? {};
    let worldSavePath = null;

    if (session.worldCapture?.chunkCount > 0 && sniffer.saveLevel !== false) {
      try {
        const snapshot = session.worldCapture.getExportSnapshot();
        const result = await exportSessionWorld({
          snapshot,
          saveDir: sniffer.saveLevelDir,
          sessionId: packetLog?.sessionId ?? `session-${Date.now()}`,
          worldName: sniffer.saveLevelName ?? packetLog?.sessionId,
        });
        worldSavePath = result?.worldDir ?? null;
      } catch (err) {
        log.error(`World save failed: ${err.message}`);
      }
    }

    if (packetLog) {
      packetLog.writeMeta({
        type: 'session_stats',
        reason,
        username: session.username,
        worldChunks: session.worldCapture?.chunkCount ?? 0,
        worldSavePath,
      });
      packetLog.close(reason);
    }
    if (worldSavePath) {
      log.info(`Level save ready: ${worldSavePath}`);
    }
  };
}

module.exports = { createMitmSession, createSessionCleanup, flushC2sQueue };

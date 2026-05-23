const path = require('path');
const mc = require('minecraft-protocol');
const { createLogger } = require('../utils/logger');
const { PacketLog } = require('./PacketLog');
const { formatTracePayload, traceBridge, traceRelay, traceTx } = require('./packetTrace');
const { relayPacket, sortLoginPending, relayToJava } = require('./mitmRelay');
const { enableJavaEncryption } = require('./mitmEncryption');
const { completeUpstreamEncryption } = require('./mitmUpstreamEncrypt');
const { applyLoginStartIdentity } = require('./mitmLogin');
const {
  shouldRelayC2SToUpstream,
  partitionAfterCrypto,
  flushPendingConfig,
} = require('./mitmLoginBridge');
const { createMitmSession, createSessionCleanup } = require('./mitmSession');
const { SnifferWorldCapture } = require('./SnifferWorldCapture');
const { startStatusPipe, startUpstream } = require('./mitmUpstream');
const { logDeserializerError } = require('./mitmWireErrors');

const log = createLogger('Sniffer');
const states = mc.states;

/** Upstream keepAlive plugin answers server pings — do not duplicate from Java. */
const BLOCK_C2S_KEEP_ALIVE = new Set(['keep_alive']);

/**
 * MITM sniffer: decrypt both legs, log packets, relay through protocol codecs.
 * Java leg uses sniffer encryption_begin; upstream leg uses real server auth/crypto.
 */
class MitmProxy {
  constructor(config) {
    this.config = config;
    this.server = null;
    this.activeSession = null;
  }

  start() {
    const sniffer = this.config.sniffer;
    this.server = mc.createServer({
      host: sniffer.host || '0.0.0.0',
      'online-mode': sniffer.onlineMode === true,
      port: sniffer.port,
      version: this.config.server.version,
      maxPlayers: 1,
      motd: '§eMITM Sniffer',
      kickTimeout: 120000,
      checkTimeoutInterval: 10000,
      hideErrors: true,
      errorHandler: (_client, err) => {
        log.error('Client error:', err.message);
      },
    });

    this.server.on('connection', (client) => this._onConnection(client));

    this.server.on('listening', () => {
      log.info(
        `MITM sniffer on ${sniffer.host || '0.0.0.0'}:${sniffer.port} → ${this.config.server.host}:${this.config.server.port}`,
      );
      log.info(`Upstream auth: ${sniffer.upstreamAuth || 'microsoft'}`);
      log.info(`Logs: ${sniffer.logDir}`);
      if (sniffer.chunkLogDir)       log.info(`Chunk logs: ${sniffer.chunkLogDir}`);
      if (sniffer.saveLevel !== false) {
        log.info(`Level saves: ${path.resolve(sniffer.saveLevelDir)} (on disconnect)`);
      }
      log.info(`Per-packet trace: console=${sniffer.consolePacketLog !== false} every=${sniffer.logEveryPacket !== false}`);
    });

    this.server.on('error', (err) => {
      log.error('Sniffer listen error:', err.message);
    });
  }

  _onConnection(client) {
    const addr = client.socket?.remoteAddress || '?';

    if (this.activeSession) {
      log.warn(`Rejecting ${addr} — session active`);
      client.end('Sniffer allows one client at a time.');
      return;
    }

    client.removeAllListeners('login_start');

    const sniffer = this.config.sniffer;
    const worldCapture =
      sniffer.saveLevel !== false
        ? new SnifferWorldCapture({
            version: this.config.server.version,
            maxChunks: sniffer.saveLevelMaxChunks,
            enabled: true,
          })
        : null;
    const session = createMitmSession(client, null, worldCapture);
    this.activeSession = session;

    const cleanup = createSessionCleanup(session, this);

    client.on('end', () => {
      if (session.packetLog) {
        log.info(`Client disconnected ${session.username} (${addr})`);
      }
      cleanup('client_end');
    });
    client.on('error', (err) => {
      log.error(`Client error: ${err.message}`);
      logDeserializerError(session.packetLog, 'C2S', client.state, err, { leg: 'java' });
      cleanup('client_error');
    });

    client.on('packet', (data, meta, buffer) => {
      if (session.statusPipe) return;

      if (meta.state === states.HANDSHAKING && meta.name === 'set_protocol' && data.nextState === 1) {
        log.debug(`Status ping from ${addr} (no session log)`);
        startStatusPipe(session, this.config, this);
        return;
      }

      if (!session.upstream && meta.state === states.LOGIN && meta.name === 'login_start') {
        try {
          applyLoginStartIdentity(client, data, this.server, this.server.options);
        } catch (err) {
          log.error(`login_start rejected: ${err.message}`);
          client.end('Invalid login');
          return;
        }
        session.username = data.username;
        session.packetLog = this._createPacketLog(session.username);
        log.info(`Client connected ${addr} → ${session.packetLog.filePath}`);
        log.info(`Trace log: ${session.packetLog.traceFilePath}`);
        session.packetLog.writeMeta({ type: 'username', username: data.username });
        session.packetLog.writeMeta({ type: 'handshake_intent', mode: 'login' });
        this._startUpstream(session, cleanup);
        return;
      }

      if (!session.packetLog) return;

      const action = this._c2sForwardLabel(session, meta);
      session.packetLog.logPacket('C2S', meta, data, buffer, {
        leg: 'java',
        action,
        clientState: client.state,
        upstreamState: session.upstream?.state,
      });

      if (meta.name === 'login_acknowledged') {
        client.state = states.CONFIGURATION;
        this._relayC2S(session, meta, data, buffer);
        session.javaLoginAcknowledged = true;
        if (session.upstream?.state === states.LOGIN) {
          session.upstream.state = states.CONFIGURATION;
        }
        try {
          flushPendingConfig(session);
        } catch (err) {
          log.error(`login_acknowledged flush error: ${err.message}`);
        }
        return;
      }

      if (meta.name === 'finish_configuration') {
        client.state = states.PLAY;
        this._relayC2S(session, meta, data, buffer);
        session.javaFinishConfiguration = true;
        if (session.upstream?.state === states.CONFIGURATION) {
          session.upstream.state = states.PLAY;
        }
        return;
      }

      this._relayC2S(session, meta, data, buffer);
    });
  }

  _createPacketLog(username) {
    const sniffer = this.config.sniffer;
    return new PacketLog({
      logDir: sniffer.logDir,
      chunkLogDir: sniffer.chunkLogDir,
      sessionId: `session-${Date.now()}`,
      clientUsername: username,
      server: `${this.config.server.host}:${this.config.server.port}`,
      version: this.config.server.version,
      includePayload: sniffer.includePayload,
      logEveryPacket: sniffer.logEveryPacket,
      consolePacketLog: sniffer.consolePacketLog,
      tracePayloadMaxLen: sniffer.tracePayloadMaxLen,
    });
  }

  _c2sForwardLabel(session, meta) {
    if (!shouldRelayC2SToUpstream(meta, session)) {
      if (meta.state === states.LOGIN && meta.name === 'encryption_begin') return 'local_java_crypto';
      return 'local';
    }
    if (BLOCK_C2S_KEEP_ALIVE.has(meta.name)) return 'blocked';
    if (!session.upstream || !session.upstreamLink) return 'queued';
    return 'relay';
  }

  _relayC2S(session, meta, data, buffer) {
    if (!shouldRelayC2SToUpstream(meta, session)) {
      if (
        meta.state === states.CONFIGURATION ||
        (meta.state === states.PLAY && !session.javaFinishConfiguration)
      ) {
        if (!session.upstreamLink) {
          session.c2sQueue.push({ meta, data, buffer });
        }
        traceBridge(session.packetLog, meta, buffer, {
          action: 'queued_java_gate',
          bridge: 'java→backend',
          dir: 'C2S',
          note:
            meta.state === states.CONFIGURATION
              ? 'waiting for Java login_acknowledged'
              : 'waiting for Java finish_configuration',
        });
        return;
      }
      if (meta.state !== states.LOGIN || meta.name !== 'encryption_begin') {
        traceBridge(session.packetLog, meta, buffer, {
          action: this._c2sForwardLabel(session, meta),
          bridge: 'java→backend',
          dir: 'C2S',
          note: 'not relayed to backend',
        });
      }
      return;
    }
    if (BLOCK_C2S_KEEP_ALIVE.has(meta.name)) {
      traceBridge(session.packetLog, meta, buffer, {
        action: 'blocked',
        bridge: 'java→backend',
        dir: 'C2S',
        note: 'upstream keepAlive plugin handles',
      });
      return;
    }
    if (!session.upstream) return;

    if (!session.upstreamLink) {
      session.c2sQueue.push({ meta, data, buffer });
      traceBridge(session.packetLog, meta, buffer, {
        action: 'queued',
        bridge: 'java→backend',
        dir: 'C2S',
        note: `queue depth=${session.c2sQueue.length}`,
      });
      return;
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
      });
    } catch (err) {
      log.error(`C2S relay error (${meta.name}):`, err.message);
    }
  }

  _startUpstream(session, cleanup) {
    const tryAdvance = () => this._tryAdvanceLogin(session, cleanup);
    startUpstream(session, this.config, cleanup, {
      onCompressBeforeCrypto: tryAdvance,
      onEncryptionBegin: () => this._startJavaMitmCrypto(session, cleanup),
      onSuccessWhileHeld: tryAdvance,
    });
  }

  async _startJavaMitmCrypto(session, cleanup) {
    if (!session.waitingJavaCrypto || session.javaCryptoStarting || !session.upstreamEncryptRequest) {
      return;
    }
    session.javaCryptoStarting = true;

    const compressIdx = session.pendingS2C.findIndex(
      (p) => p.meta.name === 'compress' && p.meta.state === states.LOGIN,
    );
    if (compressIdx >= 0) {
      const item = session.pendingS2C[compressIdx];
      session.pendingS2C.splice(compressIdx, 1);
      try {
        const method = relayToJava(session.client, item.meta, item.data, item.buffer);
        traceTx(session.packetLog, 'java', 'S2C', item.meta, item.buffer, {
          action: 'login_compress',
          bridge: 'backend→java',
          method,
          note: 'before sniffer encryption_begin (must precede Java setEncryption)',
        });
        log.info(`Login compress → Java for ${session.username}`);
      } catch (err) {
        session.javaCryptoStarting = false;
        log.error(`Login compress to Java failed: ${err.message}`);
        session.client.end('Login compress failed');
        cleanup('compress_error');
        return;
      }
    }

    try {
      await enableJavaEncryption(session.client, this.server, this.server.options, session.packetLog);
      session.javaLegEncrypted = true;
      log.info(`Java leg encrypted for ${session.username}`);
    } catch (err) {
      session.javaCryptoStarting = false;
      log.error('Java encryption setup failed:', err.message);
      session.client.end('Sniffer encryption setup failed');
      cleanup('encryption_error');
      return;
    }

    await this._tryAdvanceLogin(session, cleanup);
  }

  async _tryAdvanceLogin(session, cleanup) {
    if (!session.javaLegEncrypted || !session.upstreamEncryptRequest) return;

    if (!session.upstreamEncryptDone) {
      try {
        await completeUpstreamEncryption(session, this.config);
        log.info(`Upstream leg encrypted for ${session.username}`);
      } catch (err) {
        log.error(`Upstream encryption failed: ${err.message}`);
        if (err.cause || err.code) log.error('Upstream encryption detail:', err.cause || err.code);
        session.client.end('Upstream encryption failed (Mojang session server)');
        cleanup('upstream_encryption_error');
        return;
      }
    }

    if (!session.pendingS2C.some((p) => p.meta.name === 'success')) return;
    if (session.javaCryptoReady) return;
    session.javaCryptoReady = true;

    sortLoginPending(session.pendingS2C);
    const heldLogin = [];
    for (const item of session.pendingS2C) {
      const { meta } = item;
      if (meta.name === 'encryption_begin') continue;
      if (meta.name === 'compress' && meta.state === states.LOGIN) {
        continue;
      }
      heldLogin.push(item);
    }
    session.pendingS2C.length = 0;
    const { login: afterCrypto, config: heldConfig } = partitionAfterCrypto(heldLogin);
    session.pendingConfig.push(...heldConfig);

    session.holdS2C = false;
    session.waitingJavaCrypto = false;
    session.packetLog.writeMeta({ type: 'java_crypto_ready' });
    log.info(`Login phase ready for ${session.username}`);

    for (const { data, meta, buffer } of afterCrypto.filter((p) => p.meta.name === 'success')) {
      try {
        const method = relayToJava(session.client, meta, data, buffer);
        traceTx(session.packetLog, 'java', 'S2C', meta, buffer, {
          action: 'post_crypto_flush',
          bridge: 'backend→java',
          method,
          note: 'held login success',
          payload: formatTracePayload(
            session.packetLog.buildFullPacketPayload(data, buffer, null),
            session.packetLog.tracePayloadMaxLen,
          ),
        });
      } catch (err) {
        log.error(`S2C success flush error: ${err.message}`);
      }
    }
  }

  stop() {
    if (this.activeSession) {
      const session = this.activeSession;
      this.activeSession = null;
      try { session.client.end('Sniffer shutting down'); } catch (_) {}
      if (session.upstream && !session.upstream.ended) {
        try { session.upstream.end('Sniffer shutting down'); } catch (_) {}
      }
      session.packetLog?.close('shutdown');
    }
    if (this.server) {
      this.server.close();
      this.server = null;
    }
  }
}

module.exports = { MitmProxy };

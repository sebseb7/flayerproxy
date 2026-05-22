const mc = require('minecraft-protocol');
const { createLogger } = require('../utils/logger');
const { PacketLog } = require('./PacketLog');
const { relayPacket, sortLoginPending, relayToJava } = require('./mitmRelay');
const { enableJavaEncryption } = require('./mitmEncryption');
const { applyLoginStartIdentity } = require('./mitmLogin');
const {
  GATE,
  canRelayC2S,
  c2sForwardLabel,
  hasPendingSuccess,
  onJavaLoginAcknowledged,
  onJavaFinishConfiguration,
  partitionAfterCrypto,
} = require('./mitmGate');
const { createMitmSession, createSessionCleanup } = require('./mitmSession');
const { startStatusPipe, startUpstream } = require('./mitmUpstream');

const log = createLogger('Sniffer');
const states = mc.states;

/**
 * MITM sniffer: Java ↔ node server ↔ upstream client ↔ real server.
 * Each leg is decrypted by minecraft-protocol so packets can be logged by name.
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
      if (sniffer.chunkLogDir) log.info(`Chunk logs: ${sniffer.chunkLogDir}`);
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

    // Do not run local login — upstream is the real server session.
    client.removeAllListeners('login_start');

    const packetLog = new PacketLog({
      logDir: this.config.sniffer.logDir,
      chunkLogDir: this.config.sniffer.chunkLogDir,
      sessionId: `session-${Date.now()}`,
      clientUsername: 'unknown',
      server: `${this.config.server.host}:${this.config.server.port}`,
      version: this.config.server.version,
      includePayload: this.config.sniffer.includePayload,
    });

    const session = createMitmSession(client, packetLog);
    this.activeSession = session;

    const cleanup = createSessionCleanup(session, packetLog, this);

    client.on('end', () => {
      log.info(`Client disconnected ${session.username} (${addr})`);
      cleanup('client_end');
    });
    client.on('error', (err) => {
      log.error(`Client error: ${err.message}`);
      cleanup('client_error');
    });

    client.on('packet', (data, meta, buffer) => {
      packetLog.logPacket('C2S', meta, data, buffer, {
        forwarded: c2sForwardLabel(session, meta),
        clientState: client.state,
        upstreamState: session.upstream?.state,
        gate: session.gate,
      });

      if (meta.state === states.HANDSHAKING && meta.name === 'set_protocol' && data.nextState === 1) {
        startStatusPipe(session, this.config, packetLog, this);
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
        packetLog.writeMeta({ type: 'username', username: data.username });
        packetLog.writeMeta({ type: 'handshake_intent', mode: 'login' });
        this._startUpstream(session, cleanup);
        return;
      }

      if (meta.name === 'login_acknowledged') {
        try {
          if (onJavaLoginAcknowledged(session)) {
            log.info(`Java login acknowledged → configuration for ${session.username}`);
          }
        } catch (err) {
          log.error(`login_acknowledged error: ${err.message}`);
        }
        return;
      }

      if (meta.name === 'finish_configuration') {
        try {
          if (onJavaFinishConfiguration(session, packetLog)) {
            log.info(`MITM bridge active (play) for ${session.username}`);
          }
        } catch (err) {
          log.error(`finish_configuration error: ${err.message}`);
        }
        return;
      }

      if (session.upstream && canRelayC2S(session, meta)) {
        try {
          relayPacket(session.upstream, meta, data, buffer);
        } catch (err) {
          log.error(`C2S relay error (${meta.name}):`, err.message);
        }
      }
    });

    log.info(`Client connected ${addr} → ${packetLog.filePath}`);
  }

  _startUpstream(session, cleanup) {
    const tryBegin = () => this._tryBeginJavaCrypto(session, cleanup);
    startUpstream(session, this.config, cleanup, {
      GATE_LOGIN: GATE.LOGIN,
      onCompressBeforeCrypto: tryBegin,
      onEncryptionBegin: tryBegin,
      onSuccessWhileHeld: tryBegin,
      onSuccessNoEncryption: (s) => {
        s.gate = GATE.AWAIT_LOGIN_ACK;
        log.info(`Login success sent (no upstream encryption) for ${s.username}`);
      },
    });
  }

  _tryBeginJavaCrypto(session, cleanup) {
    if (!session.waitingJavaCrypto || session.javaCryptoStarting || session.gate !== GATE.LOGIN) return;

    if (!hasPendingSuccess(session)) return;

    this._doJavaCrypto(session, cleanup);
  }

  async _doJavaCrypto(session, cleanup) {
    if (session.javaCryptoStarting || session.gate !== GATE.LOGIN) return;
    session.javaCryptoStarting = true;

    sortLoginPending(session.pendingS2C);
    const heldLogin = [];
    for (const item of session.pendingS2C) {
      const { meta } = item;
      if (meta.name === 'encryption_begin') continue;
      if (meta.name === 'compress' && meta.state === states.LOGIN) {
        session.relayedCompress = true;
        try {
          relayToJava(session.client, item.meta, item.data, item.buffer);
        } catch (err) {
          log.error(`S2C pre-crypto compress error:`, err.message);
        }
        continue;
      }
      heldLogin.push(item);
    }
    session.pendingS2C.length = 0;
    const { login: afterCrypto, config: heldConfig } = partitionAfterCrypto(heldLogin);
    session.pendingConfig.push(...heldConfig);

    try {
      await enableJavaEncryption(session.client, this.server, this.server.options);
    } catch (err) {
      session.javaCryptoStarting = false;
      log.error('Java encryption setup failed:', err.message);
      session.client.end('Sniffer encryption setup failed');
      cleanup('encryption_error');
      return;
    }

    session.holdS2C = false;
    session.waitingJavaCrypto = false;
    session.gate = GATE.AWAIT_LOGIN_ACK;
    session.packetLog.writeMeta({ type: 'java_crypto_ready' });
    log.info(`Java crypto ready for ${session.username}, awaiting login_acknowledged`);

    const successPackets = [
      ...afterCrypto.filter((p) => p.meta.name === 'success'),
      ...session.pendingS2C.filter((p) => p.meta.name === 'success'),
    ];
    session.pendingS2C = session.pendingS2C.filter((p) => p.meta.name !== 'success');

    for (const { data, meta, buffer } of successPackets) {
      try {
        relayToJava(session.client, meta, data, buffer);
      } catch (err) {
        log.error(`S2C success flush error:`, err.message);
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
      session.packetLog.close('shutdown');
    }
    if (this.server) {
      this.server.close();
      this.server = null;
    }
  }
}

module.exports = { MitmProxy };

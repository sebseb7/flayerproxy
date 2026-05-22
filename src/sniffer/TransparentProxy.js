const net = require('net');
const mc = require('minecraft-protocol');
const { createLogger } = require('../utils/logger');
const { PacketLog } = require('./PacketLog');
const { StreamTap } = require('./StreamTap');

const log = createLogger('Sniffer');

/**
 * TCP-transparent proxy: bytes forwarded unchanged; tap parses for JSONL only.
 */
class TransparentProxy {
  constructor(config) {
    this.config = config;
    this.server = null;
    this.activeSession = null;
  }

  start() {
    const sniffer = this.config.sniffer;
    const targetHost = this.config.server.host;
    const targetPort = this.config.server.port;

    this.server = net.createServer((clientSocket) => {
      this._onClientConnect(clientSocket, targetHost, targetPort, sniffer);
    });

    this.server.on('error', (err) => {
      log.error('Sniffer listen error:', err.message);
    });

    this.server.listen(sniffer.port, sniffer.host || '0.0.0.0', () => {
      log.info(
        `TCP sniffer on ${sniffer.host || '0.0.0.0'}:${sniffer.port} → ${targetHost}:${targetPort} (${this.config.server.version})`,
      );
      log.info('Join the server in-game (not just refresh the server list)');
      log.info(`Logs: ${sniffer.logDir}`);
      if (sniffer.chunkLogDir) log.info(`Chunk logs: ${sniffer.chunkLogDir}`);
    });
  }

  _onClientConnect(clientSocket, targetHost, targetPort, sniffer) {
    const addr = clientSocket.remoteAddress || '?';

    if (this.activeSession) {
      log.warn(`Rejecting connection from ${addr} — session already active`);
      clientSocket.destroy();
      return;
    }

    let clientUsername = 'unknown';
    const packetLog = new PacketLog({
      logDir: sniffer.logDir,
      chunkLogDir: sniffer.chunkLogDir,
      sessionId: `session-${Date.now()}`,
      clientUsername,
      server: `${targetHost}:${targetPort}`,
      version: this.config.server.version,
      includePayload: sniffer.includePayload,
    });

    const session = {
      state: mc.states.HANDSHAKING,
      compressionThreshold: -1,
      encrypted: false,
    };
    const stats = {
      rawBytes: { C2S: 0, S2C: 0 },
      frames: { C2S: 0, S2C: 0 },
      packets: { C2S: 0, S2C: 0 },
      parseErrors: { C2S: 0, S2C: 0 },
      encryptedBytes: { C2S: 0, S2C: 0 },
      encryptedChunks: { C2S: 0, S2C: 0 },
    };

    const c2sTap = new StreamTap('C2S', this.config.server.version, packetLog, {
      session,
      stats,
      onUsername: (name) => {
        clientUsername = name;
        packetLog.writeMeta({ type: 'username', username: name });
      },
    });
    const s2cTap = new StreamTap('S2C', this.config.server.version, packetLog, { session, stats });

    const upstreamSocket = net.connect({ host: targetHost, port: targetPort });
    let upstreamReady = false;
    const pendingToUpstream = [];

    this.activeSession = { clientSocket, upstreamSocket, packetLog };

    let cleaned = false;
    const cleanup = (reason) => {
      if (cleaned) return;
      cleaned = true;
      packetLog.writeMeta({
        type: 'session_stats',
        reason,
        username: clientUsername,
        protocolState: session.state,
        encrypted: session.encrypted,
        stats,
      });
      try { clientSocket.destroy(); } catch (_) {}
      try { upstreamSocket.destroy(); } catch (_) {}
      packetLog.close(reason);
      this.activeSession = null;
    };

    const flushUpstream = () => {
      for (const buf of pendingToUpstream) {
        upstreamSocket.write(buf);
      }
      pendingToUpstream.length = 0;
    };

    clientSocket.on('data', (chunk) => {
      if (upstreamReady && !upstreamSocket.destroyed) {
        upstreamSocket.write(chunk);
      } else {
        pendingToUpstream.push(chunk);
      }
      c2sTap.feed(chunk);
    });

    upstreamSocket.on('data', (chunk) => {
      if (!clientSocket.destroyed) clientSocket.write(chunk);
      s2cTap.feed(chunk);
    });

    upstreamSocket.on('connect', () => {
      upstreamReady = true;
      flushUpstream();
      log.info(`Upstream TCP connected (${addr} → ${targetHost}:${targetPort})`);
      packetLog.writeMeta({ type: 'upstream_connect' });
    });

    upstreamSocket.on('error', (err) => {
      log.error(`Upstream error: ${err.message}`);
      cleanup('upstream_error');
    });

    clientSocket.on('error', (err) => {
      log.error(`Client socket error: ${err.message}`);
      cleanup('client_error');
    });

    upstreamSocket.on('close', () => {
      log.info(`Session ended (upstream closed) ${clientUsername} (${addr})`);
      cleanup('upstream_close');
    });

    clientSocket.on('close', () => {
      if (!cleaned) {
        log.info(`Session ended (client closed) ${clientUsername} (${addr})`);
        cleanup('client_close');
      }
    });

    log.info(`Client connected ${addr} — logging to ${packetLog.filePath}`);
  }

  stop() {
    if (this.activeSession) {
      const { clientSocket, upstreamSocket, packetLog } = this.activeSession;
      try { clientSocket.destroy(); } catch (_) {}
      try { upstreamSocket.destroy(); } catch (_) {}
      packetLog.close('shutdown');
      this.activeSession = null;
    }
    if (this.server) {
      this.server.close();
      this.server = null;
    }
  }
}

module.exports = { TransparentProxy };

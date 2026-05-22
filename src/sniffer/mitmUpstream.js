const net = require('net');
const mc = require('minecraft-protocol');
const { createLogger } = require('../utils/logger');
const { relayToJava, syncCompression } = require('./mitmRelay');
const { classifyS2C, queueHeldS2C, queueBufferedS2C } = require('./mitmGate');

const log = createLogger('Sniffer');
const states = mc.states;

/**
 * Pipe a status/ping handshake directly at the TCP level (no decryption needed).
 * @param {object} session
 * @param {{ server: { host: string, port: number } }} config
 * @param {import('./PacketLog').PacketLog} packetLog
 * @param {{ activeSession: object|null }} proxy - MitmProxy instance
 */
function startStatusPipe(session, config, packetLog, proxy) {
  const { host, port } = config.server;
  const upstream = net.connect({ host, port });
  session.statusPipe = { client: session.client.socket, upstream };

  packetLog.writeMeta({ type: 'handshake_intent', mode: 'status_ping' });

  session.client.socket.pipe(upstream);
  upstream.pipe(session.client.socket);

  upstream.on('connect', () => packetLog.writeMeta({ type: 'upstream_connect', mode: 'status_tcp' }));
  let statusDone = false;
  const endStatus = () => {
    if (statusDone) return;
    statusDone = true;
    if (proxy.activeSession === session) proxy.activeSession = null;
    packetLog.close('status_done');
  };
  upstream.on('close', endStatus);
  session.client.socket.on('close', endStatus);
}

/**
 * Create the upstream mc.createClient and wire the S2C packet relay.
 * @param {object} session
 * @param {{ server: { host: string, port: number, version: string }, sniffer: { upstreamAuth?: string } }} config
 * @param {function(string): void} cleanup
 * @param {object} callbacks - { onCompressBeforeCrypto, onEncryptionBegin, onSuccessNoEncryption }
 */
function startUpstream(session, config, cleanup, callbacks) {
  const { host, port, version } = config.server;
  const auth = config.sniffer.upstreamAuth || 'microsoft';

  const upstream = mc.createClient({
    host,
    port,
    username: session.username,
    version,
    auth,
    hideErrors: true,
    keepAlive: true,
    checkTimeoutInterval: 60000,
  });
  session.upstream = upstream;

  upstream.on('connect', () => {
    log.info(`Upstream connected for ${session.username}`);
    session.packetLog.writeMeta({ type: 'upstream_connect' });
  });

  upstream.on('packet', (data, meta, buffer) => {
    const s2cAction = classifyS2C(session, meta);
    session.packetLog.logPacket('S2C', meta, data, buffer, {
      forwarded: s2cAction,
      clientState: session.client.state,
      upstreamState: upstream.state,
      gate: session.gate,
    });
    syncCompression(upstream, meta.name, data);

    if (s2cAction === 'relay') {
      try {
        relayToJava(session.client, meta, data, buffer);
      } catch (err) {
        log.error(`S2C relay error (${meta.name}):`, err.message);
      }
      return;
    }

    if (s2cAction === 'buffer') {
      queueBufferedS2C(session, data, meta, buffer);
      return;
    }

    if (session.gate !== callbacks.GATE_LOGIN) {
      return;
    }

    if (meta.name === 'compress' && meta.state === states.LOGIN) {
      session.relayedCompress = true;
      try {
        relayToJava(session.client, meta, data, buffer);
      } catch (err) {
        log.error(`S2C compress error:`, err.message);
      }
      callbacks.onCompressBeforeCrypto(session);
      return;
    }

    if (meta.name === 'encryption_begin') {
      session.holdS2C = true;
      session.waitingJavaCrypto = true;
      callbacks.onEncryptionBegin(session);
      return;
    }

    if (session.holdS2C) {
      queueHeldS2C(session, data, meta, buffer);
      if (meta.name === 'success') {
        callbacks.onSuccessWhileHeld(session);
      }
      return;
    }

    if (meta.name === 'success') {
      try {
        relayToJava(session.client, meta, data, buffer);
        callbacks.onSuccessNoEncryption(session);
      } catch (err) {
        log.error(`S2C success error:`, err.message);
      }
      return;
    }

    try {
      relayToJava(session.client, meta, data, buffer);
    } catch (err) {
      log.error(`S2C relay error (${meta.name}):`, err.message);
    }
  });

  upstream.on('end', () => {
    log.info(`Upstream closed for ${session.username}`);
    if (!session.cleaned && !session.client.ended) {
      try { session.client.end('Upstream disconnected'); } catch (_) {}
    }
    cleanup('upstream_end');
  });

  upstream.on('error', (err) => {
    log.error(`Upstream error: ${err.message}`);
    if (!session.cleaned && !session.client.ended) {
      try { session.client.end(err.message); } catch (_) {}
    }
    cleanup('upstream_error');
  });
}

module.exports = { startStatusPipe, startUpstream };

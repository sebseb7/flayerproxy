const net = require('net');
const mc = require('minecraft-protocol');
const { createLogger } = require('../utils/logger');
const { CHUNK_PACKETS, ENTITY_PACKETS, META_PACKETS } = require('./SnifferWorldCapture');
const { createPassiveClient } = require('./mitmPassiveClient');
const { relayToJava, syncCompression } = require('./mitmRelay');
const { flushC2sQueue } = require('./mitmSession');
const { traceBridge, traceRelay, traceTx } = require('./packetTrace');
const { queueHeldS2C } = require('./mitmLoginBridge');
const { logDeserializerError } = require('./mitmWireErrors');

const log = createLogger('Sniffer');
const states = mc.states;

function startStatusPipe(session, config, proxy) {
  const { host, port } = config.server;
  const upstream = net.connect({ host, port });
  session.statusPipe = { client: session.client.socket, upstream };

  session.client.socket.pipe(upstream);
  upstream.pipe(session.client.socket);

  let statusDone = false;
  const endStatus = () => {
    if (statusDone) return;
    statusDone = true;
    if (proxy.activeSession === session) proxy.activeSession = null;
  };
  upstream.on('close', endStatus);
  session.client.socket.on('close', endStatus);
}

function startUpstream(session, config, cleanup, callbacks) {
  const { host, port, version } = config.server;
  const auth = config.sniffer.upstreamAuth || 'microsoft';

  const upstream = createPassiveClient({
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
    session.upstreamLink = true;
    log.info(`Upstream connected for ${session.username}`);
    session.packetLog.writeMeta({ type: 'upstream_connect' });
    try {
      flushC2sQueue(session);
    } catch (err) {
      log.error(`C2S queue flush error: ${err.message}`);
      try { session.client.end(err.message); } catch (_) {}
      cleanup('c2s_flush_error');
    }
  });

  upstream.on('packet', (data, meta, buffer) => {
    const isLoginEncrypt =
      meta.name === 'encryption_begin' && meta.state === states.LOGIN;
    const holdConfigS2C =
      meta.state === states.CONFIGURATION && !session.javaLoginAcknowledged;
    const forwarded = isLoginEncrypt
      ? 'mitm'
      : holdConfigS2C
        ? 'hold_java_ack'
        : session.holdS2C
          ? 'hold'
          : 'relay';

    session.packetLog.logPacket('S2C', meta, data, buffer, {
      leg: 'backend',
      dir: 'S2C',
      action: forwarded,
      clientState: session.client.state,
      upstreamState: upstream.state,
      note: holdConfigS2C ? 'waiting for Java login_acknowledged' : undefined,
    });

    if (
      session.worldCapture &&
      meta.state === states.PLAY &&
      (CHUNK_PACKETS.has(meta.name) ||
        ENTITY_PACKETS.has(meta.name) ||
        META_PACKETS.has(meta.name))
    ) {
      session.worldCapture.handleServerPacket(meta.name, data);
    }

    syncCompression(upstream, meta.name, data);

    if (isLoginEncrypt) {
      session.upstreamEncryptRequest = data;
      session.holdS2C = true;
      session.waitingJavaCrypto = true;
      traceBridge(session.packetLog, meta, buffer, {
        action: 'mitm',
        bridge: 'backend→java',
        dir: 'S2C',
        note: 'held; upstream encrypt waits for Java sniffer encryption_begin',
      });
      callbacks.onEncryptionBegin?.(session);
      return;
    }

    if (holdConfigS2C) {
      queueHeldS2C(session, data, meta, buffer);
      return;
    }

    if (meta.name === 'compress' && meta.state === states.LOGIN) {
      if (session.holdS2C) {
        queueHeldS2C(session, data, meta, buffer);
        return;
      }
      syncCompression(session.client, meta.name, data);
      try {
        const method = relayToJava(session.client, meta, data, buffer);
        traceTx(session.packetLog, 'java', 'S2C', meta, buffer, {
          action: 'relay',
          bridge: 'backend→java',
          method,
          note: 'login compress before crypto hold',
        });
      } catch (err) {
        log.error(`S2C compress error: ${err.message}`);
      }
      callbacks.onCompressBeforeCrypto?.(session);
      return;
    }

    if (session.holdS2C) {
      queueHeldS2C(session, data, meta, buffer);
      if (meta.name === 'success') callbacks.onSuccessWhileHeld?.(session);
      return;
    }

    syncCompression(session.client, meta.name, data);

    try {
      const method = relayToJava(session.client, meta, data, buffer);
      traceRelay(session.packetLog, {
        bridge: 'backend→java',
        dir: 'S2C',
        meta,
        data,
        buffer,
        method,
      });
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
    logDeserializerError(session.packetLog, 'S2C', upstream.state, err, { leg: 'backend' });
    if (!session.cleaned && !session.client.ended) {
      try { session.client.end(err.message); } catch (_) {}
    }
    cleanup('upstream_error');
  });
}

module.exports = { startStatusPipe, startUpstream };

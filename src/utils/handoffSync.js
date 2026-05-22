/**
 * Helpers aligned with vanilla join flow (PlayerList.placeNewPlayer, PlayerChunkSender).
 */

const { RAW_FORWARD_PACKETS } = require('../constants/rawPackets');
const { ensureClientViewIncludesChunk } = require('./positionSync');

const { logJavaC2S, logProxyC2S } = require('./handoffTrace');

/** Per-handoff gate: hold java player_loaded until entities/inventory are replayed. */
function createHandoffUpstreamGate() {
  return { holdPlayerLoaded: true, pendingPlayerLoaded: null };
}

function installHandoffUpstreamRelay(client, serverConn, log, gate) {
  const handler = (data, meta) => {
    if (meta.state !== 'play') return;
    if (meta.name === 'chunk_batch_received') {
      logJavaC2S(log, meta.name, data, 'HANDOFF');
      serverConn.writeToServer('chunk_batch_received', data, {
        source: 'handoffRelay.forward(java)',
      });
    } else if (meta.name === 'player_loaded') {
      logJavaC2S(log, meta.name, data, 'HANDOFF');
      if (gate?.holdPlayerLoaded) {
        gate.pendingPlayerLoaded = data ?? {};
        if (log) {
          log.info(
            '[Handoff] HOLD java player_loaded — will forward upstream after entities+inventory replay',
          );
        }
        return;
      }
      serverConn.writeToServer('player_loaded', data, {
        source: 'handoffRelay.forward(java)',
      });
    } else if (meta.name === 'teleport_confirm') {
      logJavaC2S(log, meta.name, data, 'HANDOFF');
      serverConn.writeToServer('teleport_confirm', data, {
        source: 'handoffRelay.forward(java)',
      });
    }
  };
  client.on('packet', handler);
  return handler;
}

function removeHandoffUpstreamRelay(client, handler) {
  if (handler) client.removeListener('packet', handler);
}

/**
 * Forward player_loaded that java sent before post-terrain replay completed.
 * @returns {boolean} true if a held packet was sent upstream
 */
function releaseHeldPlayerLoaded(gate, serverConn, log) {
  if (!gate) return false;
  gate.holdPlayerLoaded = false;
  if (!gate.pendingPlayerLoaded) return false;
  const data = gate.pendingPlayerLoaded;
  gate.pendingPlayerLoaded = null;
  if (log) {
    log.info('[Handoff] RELEASE held java player_loaded → upstream (post-terrain done)');
  }
  serverConn.writeToServer('player_loaded', data, {
    source: 'handoffRelay.releaseHeld(java)',
  });
  return true;
}

/**
 * Wait until the java client finishes processing a replayed chunk batch.
 * @returns {Promise<boolean>}
 */
function waitForClientChunkBatchReceived(client, timeoutMs = 30_000, log) {
  return new Promise((resolve) => {
    if (!client || client.ended) return resolve(false);

    const timeout = setTimeout(() => {
      client.removeListener('packet', onPacket);
      if (log) log.warn('Timed out waiting for client chunk_batch_received after replay');
      resolve(false);
    }, timeoutMs);

    const onPacket = (_data, meta) => {
      if (meta.state !== 'play' || meta.name !== 'chunk_batch_received') return;
      clearTimeout(timeout);
      client.removeListener('packet', onPacket);
      if (log) log.info('Client chunk_batch_received after replay');
      resolve(true);
    };

    client.on('packet', onPacket);
  });
}

/**
 * Wait for the upstream server to push a chunk batch to the bot after player_loaded.
 * @returns {Promise<{ ok: boolean, chunkCount: number, batchSize?: number }>}
 */
function waitForServerChunkBatch(serverConn, timeoutMs = 20_000, log) {
  return new Promise((resolve) => {
    let inBatch = false;
    let chunkCount = 0;

    const handler = (name, data) => {
      if (name === 'chunk_batch_start') {
        inBatch = true;
        chunkCount = 0;
        if (log) log.info('Server chunk_batch_start (live → java client)');
      }
      if (name === 'map_chunk' && inBatch) chunkCount++;
      if (name === 'chunk_batch_finished' && inBatch) {
        cleanup();
        if (log) {
          log.info(
            `Server chunk_batch_finished batchSize=${data.batchSize} (${chunkCount} map_chunk on wire)`,
          );
        }
        resolve({ ok: true, chunkCount, batchSize: data.batchSize });
      }
    };

    const timeout = setTimeout(() => {
      cleanup();
      if (log) {
        log.warn(
          `Timed out waiting for server chunk batch (${chunkCount} map_chunk seen, inBatch=${inBatch})`,
        );
      }
      resolve({ ok: false, chunkCount });
    }, timeoutMs);

    function cleanup() {
      clearTimeout(timeout);
      serverConn.removeListener('serverPacket', handler);
    }

    serverConn.on('serverPacket', handler);
  });
}

/**
 * Wait for java client player_loaded (do not send it from the proxy).
 * @returns {Promise<boolean>}
 */
function waitForClientPlayerLoaded(client, timeoutMs = 60_000, log) {
  return new Promise((resolve) => {
    if (!client || client.ended) return resolve(false);

    const timeout = setTimeout(() => {
      client.removeListener('packet', onPacket);
      if (log) {
        log.warn(
          '[Handoff] TIMEOUT waiting for java player_loaded — proxy did NOT send player_loaded; java never sent it',
        );
      }
      resolve(false);
    }, timeoutMs);

    const onPacket = (_data, meta) => {
      if (meta.state !== 'play' || meta.name !== 'player_loaded') return;
      clearTimeout(timeout);
      client.removeListener('packet', onPacket);
      if (log) {
        log.info(
          '[Handoff] java player_loaded received (after bridge start) — NOT sent by proxy',
        );
      }
      resolve(true);
    };

    client.on('packet', onPacket);
  });
}

/** Unblock server chunk streaming after replay (client may not ack replayed batches). */
function ackChunkBatchToServer(serverConn, log) {
  const payload = { chunksPerTick: 9.0 };
  logProxyC2S(log, 'chunk_batch_received', payload, 'handoffFlow.ackChunkBatchToServer(PROXY)');
  serverConn.writeToServer('chunk_batch_received', payload, {
    source: 'handoffFlow.ackChunkBatchToServer(PROXY)',
  });
}

/**
 * Stream live map_chunk / batch markers to the proxy client during handoff (before ClientBridge).
 */
function installHandoffLiveChunkForward(client, serverConn, worldState, log) {
  const view = { chunkX: null, chunkZ: null };

  const getViewDistance = () =>
    worldState.misc.viewDistance?.viewDistance ?? 10;

  const handler = (name, data, buffer) => {
    if (
      name !== 'map_chunk' &&
      name !== 'update_light' &&
      name !== 'chunk_batch_start' &&
      name !== 'chunk_batch_finished' &&
      name !== 'update_view_position'
    ) {
      return;
    }
    if (client.ended || client.state !== 'play') return;

    try {
      if (name === 'update_view_position') {
        view.chunkX = data.chunkX;
        view.chunkZ = data.chunkZ;
      }

      const pos = serverConn.bot?.entity?.position;
      if (name === 'map_chunk' && pos) {
        ensureClientViewIncludesChunk(
          client,
          pos.x,
          pos.z,
          data.x,
          data.z,
          getViewDistance(),
          view,
        );
      }

      const { writePlayToProxyClient } = require('./playPacketWire');
      if (RAW_FORWARD_PACKETS.has(name)) {
        writePlayToProxyClient(client, name, data);
      } else {
        client.write(name, data);
      }
    } catch (err) {
      if (log) log.debug(`Handoff live forward ${name}: ${err.message}`);
    }
  };

  serverConn.on('serverPacket', handler);
  return handler;
}

function removeHandoffLiveChunkForward(serverConn, handler) {
  if (handler) serverConn.removeListener('serverPacket', handler);
}

/** ClientboundGameEventPacket.LEVEL_CHUNKS_LOAD_START (reason 13) */
const LEVEL_CHUNKS_LOAD_START = { reason: 13, gameMode: 0 };

/** EntityEvent.PERMISSION_LEVEL_ADMINS — typical vanilla OP */
const PERMISSION_LEVEL_ADMINS = 27;

/**
 * Push cached permission entity_status to the proxy client (game mode switcher, etc.).
 */
function sendPermissionStatusToClient(client, permissionStatus, log) {
  if (!permissionStatus || !client || client.ended || client.state !== 'play') return false;
  try {
    client.write('entity_status', { ...permissionStatus });
    if (log) {
      log.info(`Sent permission entity_status ${permissionStatus.entityStatus} to client`);
    }
    return true;
  } catch (err) {
    if (log) log.error('Failed to send permission entity_status:', err.message);
    return false;
  }
}

module.exports = {
  createHandoffUpstreamGate,
  installHandoffUpstreamRelay,
  removeHandoffUpstreamRelay,
  releaseHeldPlayerLoaded,
  waitForClientChunkBatchReceived,
  waitForServerChunkBatch,
  waitForClientPlayerLoaded,
  ackChunkBatchToServer,
  installHandoffLiveChunkForward,
  removeHandoffLiveChunkForward,
  LEVEL_CHUNKS_LOAD_START,
  PERMISSION_LEVEL_ADMINS,
  sendPermissionStatusToClient,
};

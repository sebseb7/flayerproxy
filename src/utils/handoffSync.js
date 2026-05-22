/**
 * Helpers aligned with vanilla join flow (PlayerList.placeNewPlayer, PlayerChunkSender).
 */

const { RAW_FORWARD_PACKETS } = require('../constants/rawPackets');
const { ensureClientViewIncludesChunk } = require('./positionSync');

function installHandoffUpstreamRelay(client, serverConn, log) {
  const handler = (data, meta) => {
    if (meta.state !== 'play') return;
    if (meta.name === 'chunk_batch_received') {
      serverConn.writeToServer('chunk_batch_received', data);
      if (log) log.info('Forwarded client chunk_batch_received to server');
    } else if (meta.name === 'player_loaded') {
      serverConn.writeToServer('player_loaded', data);
      if (log) log.info('Forwarded client player_loaded to server');
    }
  };
  client.on('packet', handler);
  return handler;
}

function removeHandoffUpstreamRelay(client, handler) {
  if (handler) client.removeListener('packet', handler);
}

/** Unblock server chunk streaming after replay (client may not ack replayed batches). */
function ackChunkBatchToServer(serverConn, log) {
  serverConn.writeToServer('chunk_batch_received', { chunksPerTick: 9.0 });
  if (log) log.info('Sent chunk_batch_received to server after handoff replay');
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

      if (buffer?.length && RAW_FORWARD_PACKETS.has(name)) {
        client.writeRaw(buffer);
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
  installHandoffUpstreamRelay,
  removeHandoffUpstreamRelay,
  ackChunkBatchToServer,
  installHandoffLiveChunkForward,
  removeHandoffLiveChunkForward,
  LEVEL_CHUNKS_LOAD_START,
  PERMISSION_LEVEL_ADMINS,
  sendPermissionStatusToClient,
};

/**
 * Helpers aligned with vanilla join flow (PlayerList.placeNewPlayer, PlayerChunkSender).
 */

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
  LEVEL_CHUNKS_LOAD_START,
  PERMISSION_LEVEL_ADMINS,
  sendPermissionStatusToClient,
};

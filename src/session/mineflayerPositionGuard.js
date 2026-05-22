const { createLogger } = require('../utils/logger');

const log = createLogger('ServerConn');

/**
 * Mineflayer physics registers bot._client.on('position') and responds with
 * teleport_confirm + position_look on the shared upstream socket. In CLIENT_MODE
 * only the java client may drive that socket — strip mineflayer's listeners.
 */
function stashMineflayerPositionListeners(rawClient) {
  if (!rawClient || rawClient._flayerPositionStashed) return;

  const listeners = rawClient.listeners('position');
  rawClient._flayerSavedPositionListeners = listeners.slice();
  for (const fn of listeners) {
    rawClient.removeListener('position', fn);
  }
  rawClient._flayerPositionStashed = true;
  log.info(
    `Detached ${listeners.length} mineflayer position listener(s) — java client owns upstream responses`,
  );
}

function restoreMineflayerPositionListeners(rawClient) {
  if (!rawClient?._flayerPositionStashed) return;

  for (const fn of rawClient._flayerSavedPositionListeners || []) {
    rawClient.on('position', fn);
  }
  rawClient._flayerSavedPositionListeners = null;
  rawClient._flayerPositionStashed = false;
  log.info('Restored mineflayer position listeners (bot mode)');
}

module.exports = {
  stashMineflayerPositionListeners,
  restoreMineflayerPositionListeners,
};

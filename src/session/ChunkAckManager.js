const { createLogger } = require('../utils/logger');

const log = createLogger('ServerConn');

/**
 * Manages mineflayer's chunk_batch_finished auto-ack listener.
 *
 * While a Java proxy client is connected, only the client should send
 * chunk_batch_received (see PlayerChunkSender.onChunkBatchReceivedByClient).
 * This class saves and restores the mineflayer-installed listeners so we can
 * toggle ack ownership between bot and client.
 */
class ChunkAckManager {
  constructor() {
    /** @type {Function[]|null} */
    this._savedListeners = null;
  }

  /**
   * Disable mineflayer's chunk_batch_finished auto-ack.
   * @param {object} rawClient - minecraft-protocol client
   */
  disable(rawClient) {
    if (!rawClient) return;
    if (!this._savedListeners) {
      this._savedListeners = rawClient.listeners('chunk_batch_finished').slice();
    }
    rawClient.removeAllListeners('chunk_batch_finished');
    log.debug('Disabled mineflayer chunk_batch_finished auto-ack');
  }

  /**
   * Restore mineflayer's chunk_batch_finished auto-ack.
   * @param {object} rawClient - minecraft-protocol client
   */
  restore(rawClient) {
    if (!rawClient || !this._savedListeners) return;
    rawClient.removeAllListeners('chunk_batch_finished');
    for (const fn of this._savedListeners) {
      rawClient.on('chunk_batch_finished', fn);
    }
    log.debug('Restored mineflayer chunk_batch_finished auto-ack');
  }

  /**
   * Send a chunk_batch_received to unblock PlayerChunkSender.
   * @param {object} rawClient - minecraft-protocol client
   */
  flush(rawClient) {
    if (!rawClient) return;
    const payload = { chunksPerTick: 9.0 };
    try {
      const { logProxyC2S } = require('../utils/handoffTrace');
      logProxyC2S(log, 'chunk_batch_received', payload, 'ChunkAckManager.flush(PROXY)');
      rawClient.write('chunk_batch_received', payload);
    } catch (err) {
      log.debug('flushChunkBatchAck failed:', err.message);
    }
  }
}

module.exports = { ChunkAckManager };

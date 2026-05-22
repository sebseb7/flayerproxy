const { createLogger } = require('../utils/logger');
const { CHUNK_YIELD_EVERY, minChunksForHandoff, yieldEventLoop } = require('./replayHelpers');

const log = createLogger('StateReplayer');

/**
 * Replay cached chunks to a client.
 * Block and light updates are already merged into map_chunk packet data.
 *
 * @param {function(string, object): void} write - named packet writer
 * @param {function(Buffer, string): void} writeRaw - raw buffer writer
 * @param {object[]} chunks - chunks from ChunkCache.getChunksForReplay
 * @param {{ chunkX: number, chunkZ: number }} center - player chunk center
 * @param {number} totalCached - total chunks in cache (for logging)
 * @param {number} [viewDistance] - server view distance (for minimum chunk warning)
 * @returns {Promise<void>}
 */
async function replayChunks(write, writeRaw, chunks, center, totalCached, viewDistance = 10) {
  if (totalCached > chunks.length) {
    log.info(
      `Filtered ${totalCached - chunks.length} cached chunks outside view distance of bot at (${center.chunkX}, ${center.chunkZ})`
    );
  }
  const minNeeded = minChunksForHandoff(viewDistance);
  if (chunks.length === 0) {
    log.warn(
      `No cached chunks near bot at (${center.chunkX}, ${center.chunkZ}) — terrain will stream live from server after handoff`
    );
  } else if (chunks.length < minNeeded) {
    log.warn(
      `Replaying only ${chunks.length}/${minNeeded} chunks at (${center.chunkX}, ${center.chunkZ}) — live stream will fill gaps`
    );
  } else {
    log.info(`Replaying ${chunks.length} chunks around (${center.chunkX}, ${center.chunkZ})...`);
  }

  let rawChunkCount = 0;
  for (let i = 0; i < chunks.length; i++) {
    const chunk = chunks[i];
    if (chunk.rawMapChunkBuffer) {
      writeRaw(chunk.rawMapChunkBuffer, `map_chunk ${chunk.packetData.x},${chunk.packetData.z}`);
      rawChunkCount++;
    } else {
      write('map_chunk', chunk.packetData);
    }

    if ((i + 1) % CHUNK_YIELD_EVERY === 0) {
      await yieldEventLoop();
    }
  }

  write('chunk_batch_finished', { batchSize: chunks.length });
  if (rawChunkCount > 0) {
    log.info(`Replayed ${rawChunkCount}/${chunks.length} chunks from raw buffers`);
  }
}

module.exports = { replayChunks };

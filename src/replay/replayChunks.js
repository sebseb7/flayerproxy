const { createLogger } = require('../utils/logger');
const { writeMapChunkToClient } = require('../utils/mapChunkWire');
const { logMapChunkReplayed, logMapChunkReplayDone } = require('../utils/handoffTrace');
const { CHUNK_YIELD_EVERY, minChunksForHandoff, yieldEventLoop } = require('./replayHelpers');

const log = createLogger('StateReplayer');

/**
 * Replay cached chunks to a client (encode from merged column / packet data).
 *
 * @param {object} client - minecraft-protocol proxy client
 * @param {function(string, object): void} write - named packet writer
 * @param {object[]} chunks - chunks from ChunkCache.getChunksForReplay
 * @param {{ chunkX: number, chunkZ: number }} center - player chunk center
 * @param {number} totalCached - total chunks in cache (for logging)
 * @param {number} [viewDistance] - server view distance (for minimum chunk warning)
 * @returns {Promise<number>}
 */
async function replayChunks(
  proxyClient,
  write,
  chunks,
  center,
  totalCached,
  viewDistance = 10,
) {
  const replayList = chunks;
  const playerChunkX = center.chunkX;
  const playerChunkZ = center.chunkZ;
  const hasCenter = replayList.some(
    (c) => c.packetData.x === playerChunkX && c.packetData.z === playerChunkZ,
  );
  if (!hasCenter && chunks.length > 0) {
    log.warn(`Center chunk (${playerChunkX}, ${playerChunkZ}) missing from replay set`);
  }

  if (totalCached > chunks.length) {
    log.info(
      `Filtered ${totalCached - chunks.length} cached chunks outside view distance of bot at (${center.chunkX}, ${center.chunkZ})`
    );
  }
  const minNeeded = minChunksForHandoff(viewDistance);
  if (replayList.length === 0) {
    log.warn(
      `No cached chunks near bot at (${center.chunkX}, ${center.chunkZ}) — terrain will stream live from server after handoff`
    );
  } else if (replayList.length < minNeeded) {
    log.warn(
      `Replaying only ${replayList.length}/${minNeeded} chunks at (${center.chunkX}, ${center.chunkZ}) — live stream will fill gaps`
    );
  } else {
    log.info(`Replaying ${replayList.length} chunks around (${center.chunkX}, ${center.chunkZ})...`);
  }

  const counts = { encoded: 0, write: 0 };
  for (let i = 0; i < replayList.length; i++) {
    const mode = writeMapChunkToClient(proxyClient, replayList[i], write);
    counts[mode]++;
    logMapChunkReplayed(log, replayList[i].packetData.x, replayList[i].packetData.z, mode);

    if ((i + 1) % CHUNK_YIELD_EVERY === 0) {
      await yieldEventLoop();
    }
  }

  if (replayList.length > 0) {
    const parts = [];
    if (counts.encoded) parts.push(`${counts.encoded} encoded`);
    if (counts.write) parts.push(`${counts.write} fallback`);
    logMapChunkReplayDone(log, replayList.length, parts.join(', '));
  }

  return replayList.length;
}

module.exports = { replayChunks };

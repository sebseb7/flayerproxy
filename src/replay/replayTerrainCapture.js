const { createLogger } = require('../utils/logger');
const { writePlayPacketWire } = require('../utils/playPacketWire');
const { prepareMapChunkParams } = require('../state/chunkMerge');

const log = createLogger('StateReplayer');

/**
 * Replay server-captured terrain packets (encoded on the proxy client serializer).
 * @param {object} client
 * @param {{ name: string, data: object }[]} packets
 * @param {function(string, object): void} writeFallback
 * @returns {boolean}
 */
function replayCapturedTerrain(client, packets, writeFallback) {
  if (!packets.length) return false;

  let mapChunks = 0;
  for (const pkt of packets) {
    try {
      if (pkt.data == null) continue;
      if (pkt.name === 'map_chunk') {
        writePlayPacketWire(client, 'map_chunk', prepareMapChunkParams(pkt.data));
        mapChunks++;
      } else {
        writePlayPacketWire(client, pkt.name, pkt.data);
      }
    } catch (err) {
      log.warn(`Captured terrain replay ${pkt.name}: ${err.message}`);
      if (pkt.data != null) writeFallback(pkt.name, pkt.data);
    }
  }

  log.info(
    `Replayed ${packets.length} captured terrain packet(s) from server (${mapChunks} map_chunk)`,
  );
  return true;
}

/**
 * Replay primed map_chunk packets wrapped in a synthetic batch.
 */
function replayLooseCapturedMapChunks(client, loosePackets, writeFallback) {
  if (!loosePackets.length) return 0;

  writeFallback('chunk_batch_start', {});
  let sent = 0;
  for (const pkt of loosePackets) {
    try {
      if (pkt.data == null) continue;
      writePlayPacketWire(client, 'map_chunk', prepareMapChunkParams(pkt.data));
      sent++;
    } catch (err) {
      log.warn(`Loose map_chunk ${pkt.data?.x},${pkt.data?.z}: ${err.message}`);
      writeFallback('map_chunk', pkt.data);
    }
  }
  writeFallback('chunk_batch_finished', { batchSize: sent });
  log.info(`Replayed ${sent} loose-captured map_chunk(s) in synthetic batch`);
  return sent;
}

module.exports = { replayCapturedTerrain, replayLooseCapturedMapChunks };

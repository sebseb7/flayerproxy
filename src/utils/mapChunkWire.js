const { createLogger } = require('./logger');
const { prepareMapChunkParams } = require('../state/chunkMerge');
const { writePlayPacketWire } = require('./playPacketWire');

const log = createLogger('mapChunkWire');

/**
 * Encode cached map_chunk for the proxy java client (merged column state when applicable).
 *
 * @param {object} proxyClient
 * @param {{ packetData: object }} chunk
 * @param {function(string, object): void} writeFallback
 * @returns {'encoded'|'write'}
 */
function writeMapChunkToClient(proxyClient, chunk, writeFallback) {
  const label = `map_chunk ${chunk.packetData.x},${chunk.packetData.z}`;

  try {
    writePlayPacketWire(proxyClient, 'map_chunk', prepareMapChunkParams(chunk.packetData));
    return 'encoded';
  } catch (err) {
    log.warn(`${label}: encode failed (${err.message}) — client.write fallback`);
    writeFallback('map_chunk', chunk.packetData);
    return 'write';
  }
}

module.exports = { writeMapChunkToClient, prepareMapChunkParams };

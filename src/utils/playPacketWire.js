/**
 * Play-phase S2C packets that must not go through client.write (re-encode corrupts data).
 */

const { RAW_FORWARD_PACKETS } = require('../constants/rawPackets');
const { prepareMapChunkParams } = require('../state/chunkMerge');

/**
 * Always encode with the proxy client's serializer — the bot serializer uses a different
 * connection and can produce wrong packet IDs (client disconnects on debug_event, etc.).
 *
 * @param {object} proxyClient - minecraft-protocol server-side client
 */
function playSerializer(proxyClient) {
  return proxyClient.serializer;
}

/**
 * @param {object} proxyClient
 * @param {string} name
 * @param {object} data
 */
function writePlayPacketWire(proxyClient, name, data) {
  const packetBuf = playSerializer(proxyClient).createPacketBuffer({ name, params: data });
  proxyClient.writeRaw(packetBuf);
}

/**
 * Encode play S2C for the proxy java client (always proxy serializer, never upstream wire).
 * @param {object} proxyClient
 * @param {string} name
 * @param {object} data
 */
function writePlayToProxyClient(proxyClient, name, data) {
  if (name === 'map_chunk') {
    writePlayPacketWire(proxyClient, name, prepareMapChunkParams(data));
    return;
  }
  writePlayPacketWire(proxyClient, name, data);
}

module.exports = {
  playSerializer,
  writePlayPacketWire,
  writePlayToProxyClient,
};

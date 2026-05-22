const { RAW_FORWARD_PACKETS } = require('../constants/rawPackets');

/** Configuration packets forwarded with writeRaw (byte-identical NBT). */
const CONFIG_RAW_PACKETS = new Set([
  'registry_data',
  'feature_flags',
  'tags',
  'finish_configuration',
  'custom_payload',
  'reset_chat',
  'code_of_conduct',
  'server_data',
]);

const COMPRESS_PACKETS = new Set(['compress', 'set_compression']);

function shouldWriteRaw(meta, buffer) {
  if (!buffer || buffer.length === 0) return false;
  if (meta.state === 'configuration' && CONFIG_RAW_PACKETS.has(meta.name)) return true;
  if (meta.state === 'play' && RAW_FORWARD_PACKETS.has(meta.name)) return true;
  return false;
}

/**
 * @param {import('minecraft-protocol').Client} target
 */
function relayPacket(target, meta, data, buffer) {
  if (shouldWriteRaw(meta, buffer)) {
    target.writeRaw(buffer);
    return 'raw';
  }
  // Large login success (skins) must stay byte-identical once compression is negotiated.
  if (buffer?.length && meta.state === 'login' && meta.name === 'success') {
    target.writeRaw(buffer);
    return 'raw';
  }
  target.write(meta.name, data, meta.state);
  return 'parsed';
}

function syncCompression(target, name, data) {
  if (!COMPRESS_PACKETS.has(name) || data.threshold == null) return;
  target.compressionThreshold = data.threshold;
}

/** Login-phase S2C order the Java client expects (lower = earlier). */
const LOGIN_FORWARD_ORDER = {
  compress: 0,
  encryption_begin: 1,
  success: 2,
  login_plugin_request: 3,
  cookie_request: 4,
  disconnect: 99,
};

function sortLoginPending(pending) {
  pending.sort((a, b) => {
    const oa = LOGIN_FORWARD_ORDER[a.meta.name] ?? 50;
    const ob = LOGIN_FORWARD_ORDER[b.meta.name] ?? 50;
    return oa - ob;
  });
}

/**
 * Login compress must hit the wire before the compressor is enabled; otherwise
 * writeRaw adds a spurious 0-length prefix and the client fails to decode.
 */
function relayLoginCompressToJava(client, meta, data, buffer) {
  if (client.cipher != null) {
    return 'skipped_late_compress';
  }
  if (client.compressor != null) {
    syncCompression(client, meta.name, data);
    return relayPacket(client, meta, data, buffer);
  }
  if (buffer?.length) {
    client.writeRaw(buffer);
  } else {
    client.write(meta.name, data, meta.state);
  }
  syncCompression(client, meta.name, data);
  return 'login_compress';
}

/**
 * Forward S2C to Java; skip late login compress after encryption is already on.
 */
function relayToJava(client, meta, data, buffer) {
  if (meta.name === 'compress' && meta.state === 'login') {
    return relayLoginCompressToJava(client, meta, data, buffer);
  }
  return relayPacket(client, meta, data, buffer);
}

module.exports = {
  CONFIG_RAW_PACKETS,
  shouldWriteRaw,
  relayPacket,
  syncCompression,
  sortLoginPending,
  relayLoginCompressToJava,
  relayToJava,
};

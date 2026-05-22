/**
 * Replay upstream configuration packets to proxy clients after login_acknowledged.
 * Replaces minecraft-protocol login.js onClientLoginAck (empty registry + finish_configuration)
 * so replay is not followed by conflicting vanilla packets.
 */

const states = require('minecraft-protocol/src/states');
const { safeEndClient } = require('./clientDisconnect');

const CONFIGURATION_STATE = states.CONFIGURATION;

/** Upstream configuration S2C to capture (receive order preserved). */
const CONFIG_CAPTURE_NAMES = new Set([
  'registry_data',
  'feature_flags',
  'tags',
  'finish_configuration',
  'custom_payload',
  'reset_chat',
  'select_known_packs',
  'code_of_conduct',
  'server_links',
  'custom_report_details',
]);

const CONFIG_SKIP_REPLAY = new Set([
  'finish_configuration',
  'keep_alive',
  'ping',
  'disconnect',
  'cookie_request',
  'transfer',
]);

/** Captured wire bytes only for these — everything else is re-encoded on the proxy serializer. */
const CONFIG_WIRE_ONLY = new Set(['registry_data', 'custom_payload']);

function payloadForWrite(name, data) {
  if (name !== 'custom_payload' || data == null) return data;
  if (Buffer.isBuffer(data.data)) return data;
  if (data.data != null) {
    return { ...data, data: Buffer.from(data.data) };
  }
  return data;
}

/**
 * @param {object} client - minecraft-protocol server client
 * @param {string} name
 * @param {object|null} data
 * @param {Buffer|null} buffer
 */
function writeConfigPacket(client, name, data, buffer) {
  if (CONFIG_WIRE_ONLY.has(name) && buffer?.length) {
    client.writeRaw(buffer);
    return;
  }
  if (data != null) {
    const packetBuf = client.serializer.createPacketBuffer({
      name,
      params: payloadForWrite(name, data),
    });
    client.writeRaw(packetBuf);
    return;
  }
  if (buffer?.length) {
    client.writeRaw(buffer);
  }
}

/**
 * @param {object} client - minecraft-protocol server client
 * @param {import('../state/WorldStateCache').WorldStateCache} worldState
 * @param {import('../utils/logger').Logger} [log]
 * @returns {boolean}
 */
function replayConfigToClient(client, worldState, log) {
  if (!worldState.isConfigReady()) {
    if (log) {
      log.warn(
        `Skipping config replay for ${client.username || 'client'} — upstream configuration not complete`
      );
    }
    return false;
  }

  const entries = worldState.getConfigReplayEntries();
  if (entries.length === 0) {
    if (log) log.warn(`No config packets cached for ${client.username || 'client'}`);
    return false;
  }

  if (client.state !== CONFIGURATION_STATE) {
    client.state = CONFIGURATION_STATE;
  }

  let sent = 0;
  for (const { name, data, buffer } of entries) {
    if (CONFIG_SKIP_REPLAY.has(name)) continue;
    if (data == null && !buffer?.length) continue;

    try {
      writeConfigPacket(client, name, data, buffer);
      sent++;
    } catch (err) {
      if (log) log.error(`Config replay '${name}' for ${client.username}:`, err.message);
    }
  }

  if (log && sent > 0) {
    log.info(`Replayed ${sent} config packets to ${client.username}`);
  }
  return sent > 0;
}

/**
 * End a login that cannot proceed. Removes vanilla login_acknowledged handlers first —
 * otherwise login.js still sends registry/finish_configuration after end() and desyncs the client.
 */
function rejectProxyLogin(client, reason) {
  client.removeAllListeners('login_acknowledged');
  client.removeAllListeners('finish_configuration');
  safeEndClient(client, reason);
}

/**
 * Take over configuration after login_acknowledged (removes vanilla login.js handler).
 * Handler is registered synchronously on login so ack is never missed; optional
 * beforeConfigReplay runs first (e.g. wait for bot reconnect after auto-logout).
 *
 * @param {object} client
 * @param {import('minecraft-protocol').Server} mcServer
 * @param {import('../state/WorldStateCache').WorldStateCache} worldState
 * @param {import('../utils/logger').Logger} log
 * @param {{ beforeConfigReplay?: () => Promise<void> }} [hooks]
 */
function installConfigurationJoin(client, mcServer, worldState, log, hooks = {}) {
  client.removeAllListeners('login_acknowledged');
  client.once('login_acknowledged', () => {
    (async () => {
      try {
        if (hooks.beforeConfigReplay) {
          await hooks.beforeConfigReplay();
        }
        replayConfigToClient(client, worldState, log);
        client.once('finish_configuration', () => {
          client.state = states.PLAY;
          mcServer.emit('playerJoin', client);
        });
        client.write('finish_configuration', {});
      } catch (err) {
        log.error(`Configuration join failed for ${client.username}:`, err.message);
        rejectProxyLogin(client, err.message || 'Configuration failed');
      }
    })();
  });
}

module.exports = {
  CONFIG_CAPTURE_NAMES,
  replayConfigToClient,
  rejectProxyLogin,
  installConfigurationJoin,
};

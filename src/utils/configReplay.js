/**
 * Replay upstream configuration packets to proxy clients after login_acknowledged.
 * Runs on prepend (before login.js) with client.state = configuration so packet IDs match.
 * Preserves upstream receive order (registry_data before tags). login.js only sends finish_configuration.
 */

const CONFIGURATION_STATE = 'configuration';

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

/** Prefer writeRaw — payload includes restBuffer / NBT that clone+write cannot round-trip */
const CONFIG_WIRE_REPLAY = new Set(['registry_data', 'custom_payload']);

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

  // login.js onClientLoginAck has not run yet; serializer must use configuration mappings.
  if (client.state !== CONFIGURATION_STATE) {
    client.state = CONFIGURATION_STATE;
  }

  let sent = 0;
  for (const { name, data, buffer } of entries) {
    if (CONFIG_SKIP_REPLAY.has(name)) continue;
    if (data == null && !buffer?.length) continue;

    try {
      if (CONFIG_WIRE_REPLAY.has(name) && buffer?.length) {
        client.writeRaw(buffer);
      } else if (data != null) {
        client.write(name, payloadForWrite(name, data));
      }
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

module.exports = {
  CONFIG_CAPTURE_NAMES,
  replayConfigToClient,
};

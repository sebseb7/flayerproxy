const mc = require('minecraft-protocol');
const { relayToJava } = require('./mitmRelay');

const states = mc.states;

/** Java client join phases after upstream login (1.20.2+ configuration). */
const GATE = {
  LOGIN: 'login',
  AWAIT_LOGIN_ACK: 'await_login_ack',
  CONFIGURATION: 'configuration',
  PLAY: 'play',
};

const PREBRIDGE_C2S = new Set(['login_plugin_response', 'cookie_response']);

/** Upstream mc client (keepAlive: true) auto-responds; relaying from Java duplicates and kicks. */
const UPSTREAM_OWNED_C2S = new Set(['keep_alive']);

/**
 * Upstream is a separate mc client that already completed its own login/config.
 * Only relay Java C2S once both legs are in play.
 */
function canRelayC2S(session, meta) {
  if (UPSTREAM_OWNED_C2S.has(meta.name)) return false;
  if (session.gate === GATE.PLAY) return true;
  return PREBRIDGE_C2S.has(meta.name);
}

function c2sForwardLabel(session, meta) {
  if (UPSTREAM_OWNED_C2S.has(meta.name)) return 'blocked';
  if (canRelayC2S(session, meta)) return 'mitm';
  return 'pending';
}

/** How to handle upstream S2C for logging and routing. */
function classifyS2C(session, meta) {
  if (session.gate === GATE.PLAY) return 'relay';
  if (session.gate === GATE.CONFIGURATION && meta.state === states.CONFIGURATION) return 'relay';
  if (shouldBufferS2C(session, meta)) return 'buffer';
  return 'hold';
}

function shouldBufferS2C(session, meta) {
  if (session.gate === GATE.PLAY) return false;
  if (session.gate === GATE.CONFIGURATION) return meta.state === states.PLAY;
  if (session.gate === GATE.AWAIT_LOGIN_ACK) return true;
  return false;
}

function flushQueue(session, queue) {
  for (const { data, meta, buffer } of queue) {
    try {
      relayToJava(session.client, meta, data, buffer);
    } catch (err) {
      throw new Error(`${meta.state}.${meta.name}: ${err.message}`);
    }
  }
  queue.length = 0;
}

function flushPendingConfig(session) {
  flushQueue(session, session.pendingConfig);
}

/** Join-critical play packets before terrain chunks. */
const PLAY_JOIN_ORDER = {
  login: 0,
  custom_payload: 1,
  server_data: 2,
  difficulty: 3,
  abilities: 4,
  held_item_slot: 5,
  recipe_book_settings: 6,
  recipe_book_add: 7,
  entity_status: 8,
  declare_recipes: 9,
  position: 10,
  player_info: 11,
  update_view_distance: 12,
  simulation_distance: 13,
  spawn_position: 14,
  initialize_world_border: 15,
  update_time: 16,
  game_state_change: 17,
  set_ticking_state: 18,
  step_tick: 19,
  window_items: 20,
  set_slot: 21,
  system_chat: 22,
  declare_commands: 23,
  update_health: 24,
  experience: 25,
};

const PLAY_CHUNK_PACKETS = new Set([
  'map_chunk',
  'update_light',
  'unload_chunk',
  'chunk_batch_start',
  'chunk_batch_finished',
]);

function sortPlayPending(pending) {
  pending.sort((a, b) => {
    const oa = PLAY_JOIN_ORDER[a.meta.name] ?? 100;
    const ob = PLAY_JOIN_ORDER[b.meta.name] ?? 100;
    return oa - ob;
  });
}

function isStalePlayS2C(meta) {
  return meta.name === 'keep_alive';
}

function flushPendingPlay(session) {
  sortPlayPending(session.pendingPlay);
  const join = [];
  const world = [];
  for (const item of session.pendingPlay) {
    if (isStalePlayS2C(item.meta)) continue;
    if (PLAY_CHUNK_PACKETS.has(item.meta.name)) world.push(item);
    else join.push(item);
  }
  session.pendingPlay.length = 0;
  flushQueue(session, join);
  if (world.length) {
    setImmediate(() => {
      flushQueue(session, world);
    });
  }
}

function queueBufferedS2C(session, data, meta, buffer) {
  if (isStalePlayS2C(meta)) return;
  const item = { data, meta, buffer };
  if (meta.state === states.PLAY) {
    session.pendingPlay.push(item);
  } else {
    session.pendingConfig.push(item);
  }
}

function hasPendingSuccess(session) {
  return session.pendingS2C.some((p) => p.meta.name === 'success');
}

function onJavaLoginAcknowledged(session) {
  if (session.gate !== GATE.AWAIT_LOGIN_ACK) return false;
  session.client.state = states.CONFIGURATION;
  session.gate = GATE.CONFIGURATION;
  flushPendingConfig(session);
  return true;
}

function onJavaFinishConfiguration(session, packetLog) {
  if (session.gate !== GATE.CONFIGURATION) return false;
  session.client.state = states.PLAY;
  session.gate = GATE.PLAY;
  session.bridged = true;
  flushPendingPlay(session);
  packetLog.writeMeta({ type: 'bridge_active' });
  return true;
}

function queueHeldS2C(session, data, meta, buffer) {
  const item = { data, meta, buffer };
  if (meta.state === states.CONFIGURATION) {
    session.pendingConfig.push(item);
  } else {
    session.pendingS2C.push(item);
  }
}

function partitionAfterCrypto(pendingS2C) {
  const login = [];
  const config = [];
  for (const item of pendingS2C) {
    if (item.meta.state === states.CONFIGURATION) config.push(item);
    else login.push(item);
  }
  return { login, config };
}

module.exports = {
  GATE,
  canRelayC2S,
  c2sForwardLabel,
  classifyS2C,
  shouldBufferS2C,
  hasPendingSuccess,
  flushPendingConfig,
  flushPendingPlay,
  onJavaLoginAcknowledged,
  onJavaFinishConfiguration,
  queueHeldS2C,
  queueBufferedS2C,
  partitionAfterCrypto,
};

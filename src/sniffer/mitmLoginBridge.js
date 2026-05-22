const mc = require('minecraft-protocol');
const { relayToJava } = require('./mitmRelay');

const states = mc.states;

/** Login-state ids that are really configuration S2C when upstream decoder lags. */
const LOGIN_ID_LIKELY_CONFIG = {
  12: 'feature_flags',
  14: 'select_known_packs',
};

function formatPacketName(meta, peerState) {
  const name = meta.name;
  if (typeof name !== 'number') return name;
  if (meta.state === states.LOGIN && peerState === states.CONFIGURATION) {
    const hint = LOGIN_ID_LIKELY_CONFIG[name];
    if (hint) return `${name}(likely configuration.${hint})`;
  }
  return String(name);
}

/** Java leg only — upstream already sends its own hello via Microsoft auth. */
function shouldRelayC2SToUpstream(meta, session) {
  if (meta.state === states.HANDSHAKING) return false;
  if (meta.state === states.LOGIN && meta.name === 'login_start') return false;
  /** Java encryption_begin answers sniffer RSA — upstream uses completeUpstreamEncryption after Java. */
  if (meta.state === states.LOGIN && meta.name === 'encryption_begin') return false;
  /** Configuration/play C2S only after Java has sent login_acknowledged. */
  if (meta.state === states.CONFIGURATION && session && !session.javaLoginAcknowledged) return false;
  if (meta.state === states.PLAY && session && !session.javaFinishConfiguration) return false;
  return true;
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

function queueHeldS2C(session, data, meta, buffer) {
  const { traceBridge } = require('./packetTrace');
  traceBridge(session.packetLog, meta, buffer, {
    event: 'hold',
    leg: 'backend',
    dir: 'S2C',
    action: 'hold',
    bridge: 'backend→java',
    note: `queued pending=${session.pendingS2C.length + 1}`,
  });
  const item = { data, meta, buffer };
  if (meta.state === states.CONFIGURATION) {
    session.pendingConfig.push(item);
  } else {
    session.pendingS2C.push(item);
  }
}

function flushPendingConfig(session) {
  const { traceTx } = require('./packetTrace');
  for (const { data, meta, buffer } of session.pendingConfig) {
    const method = relayToJava(session.client, meta, data, buffer);
    traceTx(session.packetLog, 'java', 'S2C', meta, buffer, {
      action: 'flush_config',
      bridge: 'backend→java',
      method,
      note: 'login_acknowledged flush',
    });
  }
  session.pendingConfig.length = 0;
}

module.exports = {
  formatPacketName,
  shouldRelayC2SToUpstream,
  partitionAfterCrypto,
  queueHeldS2C,
  flushPendingConfig,
};

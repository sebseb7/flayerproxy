'use strict';

const mc = require('minecraft-protocol');

const states = mc.states;

/** Login-state ids that are really configuration S2C when upstream decoder lags. */
const LOGIN_ID_LIKELY_CONFIG = {
  12: 'feature_flags',
  14: 'select_known_packs',
};

/**
 * @param {object} meta - minecraft-protocol metadata (name may be numeric when unmapped)
 * @param {object} [extra] - logPacket extras (clientState / upstreamState for hints)
 */
function resolvePacketName(meta, extra = {}) {
  const raw = meta.name;
  if (typeof raw !== 'number') {
    return { name: raw, packetId: null, unknown: false, displayName: raw, note: null };
  }

  const peerState = extra.clientState ?? extra.upstreamState;
  const hint =
    meta.state === states.LOGIN && peerState === states.CONFIGURATION
      ? LOGIN_ID_LIKELY_CONFIG[raw]
      : null;

  if (hint) {
    return {
      name: `unknown_${hint}`,
      packetId: raw,
      unknown: true,
      displayName: `${raw}(likely configuration.${hint})`,
      note: `likely configuration.${hint}`,
    };
  }

  const hex = `0x${raw.toString(16)}`;
  return {
    name: 'unknown',
    packetId: raw,
    unknown: true,
    displayName: `unknown:${hex}`,
    note: `unmapped packet id ${hex} in state ${meta.state}`,
  };
}

/** @deprecated use resolvePacketName(meta, { clientState }).displayName */
function formatPacketName(meta, peerState) {
  return resolvePacketName(meta, { clientState: peerState }).displayName;
}

module.exports = { resolvePacketName, formatPacketName, LOGIN_ID_LIKELY_CONFIG };

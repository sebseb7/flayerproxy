import { PLAY, CFG } from './constants.js';

/** @param {NodeJS.ProcessEnv} [env] @param {'client' | 'server'} [role] */
export function logPingTickFromEnv(env = process.env, role = 'client') {
  if (env.MC_LOG_PING_TICK === '1') return true;
  if (role === 'client' && env.MC_CLIENT_LOG_PING_TICK === '1') return true;
  if (role === 'server') {
    return env.MC_SERVER_LOG_PING_TICK === '1' || env.MC_CLIENT_LOG_PING_TICK === '1';
  }
  return false;
}

/** @param {number} id */
export function isNoisyS2cPacket(id) {
  return (
    id === PLAY.PING ||
    id === CFG.PING ||
    id === PLAY.KEEP_ALIVE ||
    id === CFG.KEEP_ALIVE
  );
}

/** @param {number} id */
export function isNoisyC2sPacket(id) {
  return (
    id === PLAY.C2S_TICK_END ||
    id === PLAY.C2S_PONG ||
    id === CFG.C2S_PONG ||
    id === PLAY.C2S_KEEP_ALIVE ||
    id === CFG.C2S_KEEP_ALIVE
  );
}

// Cached list of blocked packets/ids
let blockedPackets = null;

function getBlockedPackets() {
  if (blockedPackets !== null) return blockedPackets;

  blockedPackets = new Set();

  const envVal = process.env.MC_LOG_BLOCK || process.env.MC_CLIENT_LOG_BLOCK;
  if (envVal) {
    for (const p of envVal.split(',')) {
      blockedPackets.add(p.trim().toLowerCase());
    }
  }

  for (const arg of process.argv) {
    if (arg.startsWith('--block-packets=')) {
      const val = arg.split('=')[1];
      if (val) {
        for (const p of val.split(',')) {
          blockedPackets.add(p.trim().toLowerCase());
        }
      }
    }
  }

  return blockedPackets;
}

/**
 * Check if a packet should be blocked from logging based on name or ID.
 * @param {string|null} name
 * @param {number} id
 * @returns {boolean}
 */
export function isBlockedPacket(name, id) {
  const blocked = getBlockedPackets();
  if (blocked.size === 0) return false;

  if (name && blocked.has(name.toLowerCase())) {
    return true;
  }
  if (id !== undefined) {
    const idHex = '0x' + id.toString(16).toLowerCase();
    const idHexPad = '0x' + id.toString(16).padStart(2, '0').toLowerCase();
    const idStr = id.toString();
    if (blocked.has(idHex) || blocked.has(idHexPad) || blocked.has(idStr)) {
      return true;
    }
  }
  return false;
}

/**
 * Returns the sorted list of blocked packet names/ids (for logging).
 * @returns {string[]}
 */
export function getBlockedPacketsList() {
  const blocked = getBlockedPackets();
  return Array.from(blocked).sort();
}

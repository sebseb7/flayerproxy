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

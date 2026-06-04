import { LOG_LEVELS } from './constants.js';
import { logPingTickFromEnv } from './logNoise.js';

function positionalArgs(argv) {
  return argv.slice(2).filter((a) => !a.startsWith('--'));
}

export function loadConfig(argv = process.argv, env = process.env) {
  const debug = env.MC_CLIENT_DEBUG === '1';
  const logPingTick = logPingTickFromEnv(env, 'client');
  const logLevelName = env.MC_CLIENT_LOG_LEVEL;
  const logLevel = LOG_LEVELS[logLevelName] ?? (debug ? LOG_LEVELS.debug : LOG_LEVELS.info);
  const autoRespawn = argv.includes('--auto-respawn') || env.MC_CLIENT_AUTO_RESPAWN === '1';

  const logFileEnv = env.MC_CLIENT_LOG_FILE;
  const logFile =
    logFileEnv !== undefined
      ? logFileEnv || null
      : debug
        ? 'logout'
        : null;

  const pos = positionalArgs(argv);

  return {
    host: pos[0] || '127.0.0.1',
    port: Number(pos[1] || 25565),
    username: pos[2] || 'TestBot',
    debug,
    logPingTick,
    logLevel,
    logFile,
    autoRespawn,
  };
}

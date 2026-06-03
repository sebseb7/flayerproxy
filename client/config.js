import { LOG_LEVELS } from './constants.js';
import { logPingTickFromEnv } from './logNoise.js';

export function loadConfig(argv = process.argv, env = process.env) {
  const debug = env.MC_CLIENT_DEBUG === '1';
  const logPingTick = logPingTickFromEnv(env, 'client');
  const logLevelName = env.MC_CLIENT_LOG_LEVEL;
  const logLevel = LOG_LEVELS[logLevelName] ?? (debug ? LOG_LEVELS.debug : LOG_LEVELS.info);

  const logFileEnv = env.MC_CLIENT_LOG_FILE;
  const logFile =
    logFileEnv !== undefined
      ? logFileEnv || null
      : debug
        ? 'logout'
        : null;

  return {
    host: argv[2] || '127.0.0.1',
    port: Number(argv[3] || 25565),
    username: argv[4] || 'TestBot',
    debug,
    logPingTick,
    logLevel,
    logFile,
  };
}

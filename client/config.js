import fs from 'node:fs';
import { execSync } from 'node:child_process';
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
  const host = pos[0] || '127.0.0.1';
  let username = pos[2] || env.MC_USERNAME || 'FlayerBot';

  let accessToken = env.MC_ACCESS_TOKEN || null;
  let profileId = env.MC_PROFILE_ID || null;

  const isLocal = host === '127.0.0.1' || host === 'localhost';
  const wantOnlineAuth = env.MC_OFFLINE !== '1' && (!isLocal || username !== 'FlayerBot');
  if (wantOnlineAuth && (!accessToken || !profileId)) {
    console.error(`MSA login for ${username} (browser/device code may appear)...`);
    const root = env.FLAYERPROXY_ROOT || process.cwd();
    const scriptPath = `${root}/libchunk/scripts/msa_token.js`;
    if (fs.existsSync(scriptPath)) {
      const cmd = `node "${scriptPath}" "${username}"`;
      try {
        const stdout = execSync(cmd, { stdio: ['inherit', 'pipe', 'inherit'] }).toString();
        const matchToken = stdout.match(/"accessToken"\s*:\s*"([^"]+)"/);
        const matchProfile = stdout.match(/"profileId"\s*:\s*"([^"]+)"/);
        const matchUsername = stdout.match(/"username"\s*:\s*"([^"]+)"/);
        if (matchToken && matchProfile) {
          accessToken = matchToken[1];
          profileId = matchProfile[1];
          if (matchUsername) {
            username = matchUsername[1];
          }
        } else {
          console.error('warning: no credentials; online-mode servers will reject login');
        }
      } catch (e) {
        console.error('warning: no credentials; online-mode servers will reject login');
      }
    } else {
      console.error('warning: no credentials; online-mode servers will reject login');
    }
  }

  return {
    host,
    port: Number(pos[1] || 25565),
    username,
    debug,
    logPingTick,
    logLevel,
    logFile,
    autoRespawn,
    accessToken,
    profileId,
  };
}


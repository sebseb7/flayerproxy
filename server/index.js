import net from 'node:net';
import chalk from 'chalk';
import { LOG_LEVELS } from '../client/constants.js';
import { createServerLogger } from './logger.js';
import { logPingTickFromEnv } from '../client/logNoise.js';
import { handleClient } from './session.js';
import { getCapture } from '../client/captureStore.js';

export function startCaptureServer(options = {}) {
  const host = options.host ?? '0.0.0.0';
  const port = options.port ?? Number(process.env.MC_SERVER_PORT || 25569);
  const debug =
    process.env.MC_SERVER_DEBUG === '1' || process.env.MC_CLIENT_DEBUG === '1';
  const logPingTick = logPingTickFromEnv(process.env, 'server');
  const logLevel =
    LOG_LEVELS[process.env.MC_SERVER_LOG_LEVEL] ??
    LOG_LEVELS[process.env.MC_CLIENT_LOG_LEVEL] ??
    (debug ? LOG_LEVELS.debug : LOG_LEVELS.info);

  const logger =
    options.logger ??
    createServerLogger({ getPhase: () => 'listen', logLevel, debug, logPingTick });

  const server = net.createServer((sock) => {
    logger.info('client connected', chalk.dim(sock.remoteAddress));
    handleClient(sock, { logLevel, debug, logPingTick });
  });

  return new Promise((resolve, reject) => {
    server.once('error', (err) => {
      if (err.code === 'EADDRINUSE') {
        reject(
          new Error(
            `port ${port} in use (mc_static_server often uses 25572). ` +
              `Try MC_SERVER_PORT=25569 or: ss -tlnp | grep ${port}`,
          ),
        );
        return;
      }
      reject(err);
    });
    server.listen(port, host, () => {
      logger.info('listening', chalk.white(`${host}:${port}`));
      const snap = getCapture();
      if (snap.ready) {
        logger.event(
          'replay ready',
          chalk.dim(`config=${snap.config.length} play_join=${snap.playJoin.length}`),
        );
      }
      resolve({ server, logger, port, host });
    });
  });
}

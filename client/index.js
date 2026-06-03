#!/usr/bin/env node

/**
 * Minimal Minecraft Java 1.21.10 (protocol 773) client for testing flayerproxy / mc_static_server.
 *
 * Usage: node client/index.js [host] [port] [username]
 *   MC_CLIENT_DEBUG=1          log every S2C/C2S packet
 *   MC_CLIENT_LOG_LEVEL=...    debug|info|warn|error
 *   MC_CLIENT_DECODE_MAX=160   truncate libchunk decode summaries
 */

import net from 'node:net';
import chalk from 'chalk';
import { loadConfig } from './config.js';
import { createSession } from './session.js';
import { isLibchunkLoaded, warnLibchunkLoadError } from './decode.js';
import { LOG_LEVELS } from './constants.js';

const config = loadConfig();
const session = createSession(config);

warnLibchunkLoadError();

session.logger.info(
  'started',
  chalk.dim(`logLevel=${Object.keys(LOG_LEVELS).find((k) => LOG_LEVELS[k] === config.logLevel)}`) +
    (config.debug ? chalk.yellow(' MC_CLIENT_DEBUG=1') : '') +
    (isLibchunkLoaded() ? chalk.green(' libchunk=ok') : chalk.yellow(' libchunk=off')),
);

const sock = net.createConnection(
  { host: config.host, port: config.port },
  () => session.onConnect(sock),
);

session.attach(sock);

process.on('SIGINT', () => {
  session.logger.warn('interrupt');
  session.stop();
  sock.destroy();
});

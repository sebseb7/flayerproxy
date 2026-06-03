#!/usr/bin/env node
'use strict';

/**
 * Minimal Minecraft Java 1.21.10 (protocol 773) client for testing flayerproxy / mc_static_server.
 *
 * Usage: node client/index.js [host] [port] [username]
 *   MC_CLIENT_DEBUG=1          log every S2C/C2S packet
 *   MC_CLIENT_LOG_LEVEL=...    debug|info|warn|error
 *   MC_CLIENT_DECODE_MAX=160   truncate libchunk decode summaries
 */

const net = require('net');
const chalk = require('chalk');
const { loadConfig } = require('./config');
const { createSession } = require('./session');
const { isLibchunkLoaded, warnLibchunkLoadError } = require('./decode');
const { LOG_LEVELS } = require('./logger');

const config = loadConfig();
const session = createSession(config);

warnLibchunkLoadError();

session.logger.info(
  'started',
  chalk.dim(
    `logLevel=${Object.keys(LOG_LEVELS).find((k) => LOG_LEVELS[k] === config.logLevel)}`,
  ) +
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

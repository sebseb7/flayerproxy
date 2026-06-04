#!/usr/bin/env node

/**
 * flayerproxy capture app (protocol 773): upstream client records config + play_join;
 * replay server starts listening only after play_join → play (capture complete).
 *
 * Usage: node client/index.js [--auto-respawn] [upstreamHost] [upstreamPort] [username]
 *   --auto-respawn           send PERFORM_RESPAWN on join death (default: off)
 *   MC_CLIENT_AUTO_RESPAWN=1 same as --auto-respawn
 *   MC_SERVER_PORT=25569
 *   MC_CLIENT_DEBUG=1        debug log level; log file defaults to logout (plain text)
 *   MC_CLIENT_LOG_FILE=path  log file path (plain, no ANSI); empty disables file logging
 *   MC_LOG_PING_TICK=1       log tick_end + ping/pong + keep_alive (default: hidden)
 *   MC_CLIENT_LOG_PING_TICK=1  same, client only
 *   MC_SERVER_LOG_PING_TICK=1  same, replay server only
 */

import fs from 'node:fs';
import net from 'node:net';
import chalk from 'chalk';
import { resetCapture, onCaptureReady } from './captureStore.js';
import { startCaptureServer } from '../server/index.js';
import { loadConfig } from './config.js';
import { createSession } from './session.js';
import { isLibchunkLoaded, warnLibchunkLoadError } from './decode.js';
import { LOG_LEVELS } from './constants.js';
import { initLogSink, writeLogLine } from './logSink.js';
import { getBlockedPacketsList } from './logNoise.js';

const serverPort = Number(process.env.MC_SERVER_PORT || 25569);
const config = loadConfig();

try {
  fs.mkdirSync('chunks', { recursive: true });
} catch (e) {}

initLogSink(config.logFile);
resetCapture();
warnLibchunkLoadError();

onCaptureReady(async () => {
  try {
    await startCaptureServer({ port: serverPort });
  } catch (e) {
    writeLogLine(`${chalk.red('mc-server')} ${e.message || e}`);
    process.exit(1);
  }
});

const session = createSession(config);
session.logger.info(
  'started',
  chalk.dim(`logLevel=${Object.keys(LOG_LEVELS).find((k) => LOG_LEVELS[k] === config.logLevel)}`) +
    (config.debug ? chalk.yellow(' MC_CLIENT_DEBUG=1') : '') +
    (config.logPingTick ? chalk.yellow(' MC_LOG_PING_TICK=1') : '') +
    (config.logFile ? chalk.dim(` logFile=${config.logFile}`) : '') +
    (isLibchunkLoaded() ? chalk.green(' libchunk=ok') : chalk.yellow(' libchunk=off')) +
    (config.autoRespawn ? chalk.yellow(' --auto-respawn') : '') +
    (() => { const bp = getBlockedPacketsList(); return bp.length ? chalk.dim(` blocked=[${bp.join(',')}]`) : ''; })()
);
session.logger.info('upstream', chalk.white(`${config.host}:${config.port}`));

const sock = net.createConnection(
  { host: config.host, port: config.port },
  () => {
    sock.setNoDelay(true);
    session.onConnect(sock);
  },
);

session.attach(sock);

process.on('SIGINT', () => {
  session.logger.warn('interrupt');
  session.stop();
  sock.destroy();
  process.exit(0);
});

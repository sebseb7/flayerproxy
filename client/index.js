import fs from 'node:fs';
import net from 'node:net';
import dns from 'node:dns/promises';
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

let connectHost = config.host;
let connectPort = config.port;

const isIp = net.isIP(config.host);
if (!isIp && config.host !== 'localhost') {
  try {
    const records = await dns.resolveSrv(`_minecraft._tcp.${config.host}`);
    if (records && records.length > 0) {
      const record = records[0];
      connectHost = record.name;
      connectPort = record.port;
      session.logger.info('dns', chalk.dim(`SRV ${config.host} -> ${connectHost}:${connectPort}`));
    }
  } catch (e) {
    // fallback to original host/port
  }
}

const sock = net.createConnection(
  { host: connectHost, port: connectPort },
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


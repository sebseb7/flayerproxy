const path = require('path');
const { loadConfig } = require('../config');
const { createLogger } = require('../utils/logger');
const { MitmProxy } = require('./MitmProxy');

const log = createLogger('SnifferMain');

console.log(`
\x1b[36m  Packet Sniffer Proxy\x1b[0m
  MITM mode — decrypt both legs, log packet names
`);

let config;
try {
  config = loadConfig();
  config.sniffer = Object.assign(
    {
      host: '0.0.0.0',
      port: 25567,
      onlineMode: false,
      upstreamAuth: 'microsoft',
      logDir: path.join(__dirname, '..', '..', 'logs', 'sniffer'),
      chunkLogDir: path.join(__dirname, '..', '..', 'logs', 'sniffer', 'chunks'),
      includePayload: true,
      logEveryPacket: true,
      consolePacketLog: true,
      tracePayloadMaxLen: 600,
      saveLevel: true,
      saveLevelDir: path.join(__dirname, '..', '..', 'logs', 'sniffer', 'worlds'),
      saveLevelPerSession: false,
      saveLevelMaxChunks: 8192,
      chunkLogEntities: false,
    },
    config.sniffer,
  );
  log.info(`Upstream: ${config.server.host}:${config.server.port} (${config.server.version})`);
  log.info(`Listen: ${config.sniffer.host || '0.0.0.0'}:${config.sniffer.port}`);
  log.info(`Client online-mode: ${config.sniffer.onlineMode}`);
  log.info(`Upstream auth: ${config.sniffer.upstreamAuth}`);
  log.info(`Logs: ${path.resolve(config.sniffer.logDir)}`);
  if (config.sniffer.chunkLog !== false) {
    const entityNote = config.sniffer.chunkLogEntities ? ' + entity packets' : '';
    log.info(
      `Chunk raw logs: ${path.resolve(config.sniffer.chunkLogDir)}/<session>/ (chunks, registry_data${entityNote})`,
    );
  }
  log.info(
    `Packet log: trace=${config.sniffer.traceLog === true || (config.sniffer.traceLog !== false && config.sniffer.logEveryPacket !== false)}, session=${config.sniffer.sessionLog === true || (config.sniffer.sessionLog !== false && config.sniffer.logEveryPacket !== false)}, chunk=${config.sniffer.chunkLog === true || (config.sniffer.chunkLog !== false && config.sniffer.logEveryPacket !== false)}, console=${config.sniffer.consolePacketLog !== false}`,
  );
  if (config.sniffer.saveLevel !== false) {
    log.info(
      `Level saves: ${path.resolve(config.sniffer.saveLevelDir)} (region/ + entities/ during session, level.dat on disconnect)`,
    );
  }
} catch (err) {
  log.error(err.message);
  process.exit(1);
}

const proxy = new MitmProxy(config);
proxy.start();

function shutdown(signal) {
  log.info(`${signal}, shutting down...`);
  proxy.stop();
  setTimeout(() => process.exit(0), 500);
}

process.on('SIGINT', () => shutdown('SIGINT'));
process.on('SIGTERM', () => shutdown('SIGTERM'));

const { loadConfig } = require('./config');
const { SessionManager } = require('./session/SessionManager');
const { createLogger } = require('./utils/logger');

const log = createLogger('Main');

// ─── Banner ──────────────────────────────────────────
console.log(`
\x1b[33m  _____ _                       ____                      
 |  ___| | __ _ _   _  ___ _ _|  _ \\ _ __ _____  ___   _ 
 | |_  | |/ _\` | | | |/ _ \\ '__| |_) | '__/ _ \\ \\/ / | | |
 |  _| | | (_| | |_| |  __/ |  |  __/| | | (_) >  <| |_| |
 |_|   |_|\\__,_|\\__, |\\___|_|  |_|   |_|  \\___/_/\\_\\\\__, |
                 |___/                                |___/ \x1b[0m
`);

// ─── Load config ─────────────────────────────────────
let config;
try {
  config = loadConfig();
  log.info(`Loaded config: server=${config.server.host}:${config.server.port} version=${config.server.version}`);
  log.info(`Play proxy port ${config.proxy.port}`);
  if (config.spectator.enabled !== false) {
    log.info(`Spectator proxy port ${config.spectator.port} (max ${config.spectator.maxClients})`);
  }
} catch (err) {
  log.error(`Failed to load config: ${err.message}`);
  process.exit(1);
}

// ─── Start session manager ──────────────────────────
const session = new SessionManager(config);
session.start();

// ─── Graceful shutdown ──────────────────────────────
let shuttingDown = false;

async function shutdown(signal) {
  if (shuttingDown) return;
  shuttingDown = true;
  log.info(`Received ${signal}, shutting down...`);
  try {
    await session.stop();
  } catch (err) {
    log.error(`Shutdown error: ${err.message}`);
  }
  setTimeout(() => process.exit(0), 100);
}

process.on('SIGINT', () => {
  shutdown('SIGINT');
});
process.on('SIGTERM', () => {
  shutdown('SIGTERM');
});

process.on('uncaughtException', (err) => {
  log.error(`Uncaught exception: ${err.message}`);
  log.error(err.stack);
});

process.on('unhandledRejection', (reason) => {
  log.error(`Unhandled rejection: ${reason}`);
});

const fs = require('fs');
const path = require('path');

const CONFIG_PATH = path.join(__dirname, '..', 'config.json');

function loadConfig() {
  if (!fs.existsSync(CONFIG_PATH)) {
    throw new Error(`Config file not found: ${CONFIG_PATH}`);
  }

  const raw = fs.readFileSync(CONFIG_PATH, 'utf-8');
  const config = JSON.parse(raw);

  // Validate required fields
  if (!config.server || !config.server.host || !config.server.port) {
    throw new Error('config.json must specify server.host and server.port');
  }
  if (!config.server.version) {
    throw new Error('config.json must specify server.version');
  }
  if (!config.auth || !config.auth.username) {
    throw new Error('config.json must specify auth.username');
  }

  // Apply defaults
  config.proxy = Object.assign({ host: '0.0.0.0', port: 25566, onlineMode: false, maxClients: 1 }, config.proxy);
  config.spectator = Object.assign(
    {
      enabled: true,
      host: '0.0.0.0',
      port: 25568,
      onlineMode: false,
      maxClients: 20,
    },
    config.spectator,
  );
  config.bot = Object.assign(
    {
      logMovement: true,
      logBridgePackets: false,
      antiAfk: true,
      antiAfkMinInterval: 1500,
      antiAfkMaxInterval: 6000,
      antiAfkInterval: 6000,
      viewDistance: 10,
      autoLogout: {
        enabled: true,
        onDamage: true,
        onPlayer: true,
        belowY: 64,
        allowedPlayers: ['tobbop2', 'craftery85'],
      },
    },
    config.bot,
  );
  config.bot.autoLogout = Object.assign(
    {
      enabled: true,
      onDamage: true,
      onPlayer: true,
      belowY: 64,
      allowedPlayers: ['tobbop2', 'craftery85'],
    },
    config.bot.autoLogout || {},
  );
  if (config.bot.autoLogout.belowY === undefined) {
    config.bot.autoLogout.belowY = 64;
  }
  if (!Array.isArray(config.bot.autoLogout.allowedPlayers)) {
    config.bot.autoLogout.allowedPlayers = ['tobbop2', 'craftery85'];
  }
  if (config.bot.antiAfkMaxInterval == null && config.bot.antiAfkInterval != null) {
    config.bot.antiAfkMaxInterval = config.bot.antiAfkInterval;
  }
  if (config.bot.antiAfkMinInterval == null && config.bot.antiAfkMaxInterval != null) {
    config.bot.antiAfkMinInterval = Math.max(500, Math.floor(config.bot.antiAfkMaxInterval / 4));
  }
  config.cache = Object.assign({ maxChunks: 1024, trackEntities: true }, config.cache);
  config.auth.auth = config.auth.auth || 'offline';

  return config;
}

module.exports = { loadConfig };

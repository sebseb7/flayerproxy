'use strict';

const DefaultServerImpl = require('minecraft-protocol/src/server');
const NodeRSA = require('node-rsa');

/** Same as minecraft-protocol createServer but without server/login.js (MITM owns login). */
const plugins = [
  require('minecraft-protocol/src/server/handshake'),
  require('minecraft-protocol/src/server/keepalive'),
  require('minecraft-protocol/src/server/ping'),
];

function createMitmSnifferServer(options = {}) {
  const {
    host = undefined,
    'server-port': serverPort,
    port = serverPort || 25565,
    motd = 'A Minecraft server',
    'max-players': maxPlayersOld = 20,
    maxPlayers: maxPlayersNew = 20,
    Server = DefaultServerImpl,
    version,
    favicon,
    customPackets,
    motdMsg,
    socketType = 'tcp',
  } = options;

  const maxPlayers = options['max-players'] !== undefined ? maxPlayersOld : maxPlayersNew;
  const optVersion =
    version === undefined || version === false
      ? require('minecraft-protocol/src/version').defaultVersion
      : version;

  const mcData = require('minecraft-data')(optVersion);
  if (!mcData) throw new Error(`unsupported protocol version: ${optVersion}`);
  const mcversion = mcData.version;
  const hideErrors = options.hideErrors || false;

  const server = new Server(mcversion.minecraftVersion, customPackets, hideErrors);
  server.mcversion = mcversion;
  server.motd = motd;
  server.motdMsg = motdMsg;
  server.maxPlayers = maxPlayers;
  server.playerCount = 0;
  server.onlineModeExceptions = Object.create(null);
  server.favicon = favicon;
  server.options = options;
  options.registryCodec =
    options.registryCodec || mcData.registryCodec || mcData.loginPacket?.dimensionCodec;

  Object.defineProperty(server, 'serverKey', {
    configurable: true,
    get() {
      this.serverKey = new NodeRSA({ b: 1024 });
      return this.serverKey;
    },
    set(value) {
      delete this.serverKey;
      this.serverKey = value;
    },
  });

  server.on('connection', (client) => {
    if (options.errorHandler) {
      client.on('error', (err) => options.errorHandler(client, err));
    }
    plugins.forEach((plugin) => plugin(client, server, options));
  });

  if (socketType === 'ipc') {
    server.listen(host);
  } else {
    server.listen(port, host);
  }
  return server;
}

module.exports = { createMitmSnifferServer };

const mc = require('minecraft-protocol');
const { createLogger } = require('../utils/logger');
const {
  wrapClientEnd,
  safeEndClient,
  disconnectServerClients,
  closeServerListenSocket,
} = require('../utils/clientDisconnect');
const { disableInboundChatValidation } = require('../utils/chatRelay');
const { replayConfigToClient } = require('../utils/configReplay');

const log = createLogger('SpectatorProxy');

/**
 * Multi-client proxy port — spectators only (read-only watch stream).
 */
class SpectatorProxyServer {
  /**
   * @param {object} config
   * @param {import('../spectator/SpectatorHub')} hub
   * @param {import('../state/WorldStateCache').WorldStateCache} worldState
   * @param {() => { ok: boolean, reason?: string }} canAcceptClient
   */
  constructor(config, hub, worldState, canAcceptClient) {
    this.config = config;
    this.hub = hub;
    this.worldState = worldState;
    this.canAcceptClient = canAcceptClient;
    this.server = null;
  }

  start() {
    const spec = this.config.spectator;
    this.server = mc.createServer({
      host: spec.host || '0.0.0.0',
      'online-mode': spec.onlineMode,
      enforceSecureProfile: false,
      port: spec.port,
      version: this.config.server.version,
      maxPlayers: spec.maxClients,
      motd: '§bFlayerProxy §7Spectators',
      hideErrors: true,
      errorHandler: (client, err) => {
        log.error(`Spectator error (${client.username || '?'}):`, err.message);
        safeEndClient(client, err);
      },
    });

    this.server.on('login', (client) => {
      wrapClientEnd(client);

      const slot = this.canAcceptClient?.() ?? { ok: true };
      if (!slot.ok) {
        log.warn(`Rejecting spectator ${client.username || 'client'}: ${slot.reason}`);
        client.end(slot.reason);
        return;
      }

      client.prependOnceListener('login_acknowledged', () => {
        replayConfigToClient(client, this.worldState, log);
      });
    });

    this.server.on('playerJoin', (client) => {
      disableInboundChatValidation(client);
      log.info(`Spectator joined: ${client.username}`);
      this.hub.addSpectator(client).catch((err) => {
        log.error(`Spectator setup failed for ${client.username}:`, err.message);
        try {
          client.end('Failed to start spectator session.');
        } catch {
          /* ignore */
        }
      });
    });

    this.server.on('error', (err) => {
      log.error('Spectator proxy error:', err.message);
    });

    this.server.on('listening', () => {
      log.info(`Spectator proxy listening on port ${spec.port}`);
    });
  }

  updateRegistryCodec(codec) {
    if (!this.server?.options) return;
    this.server.options.registryCodec = codec;
  }

  async stop() {
    await disconnectServerClients(this.server, 'Proxy shutting down');
    closeServerListenSocket(this.server);
    this.server = null;
  }
}

module.exports = { SpectatorProxyServer };

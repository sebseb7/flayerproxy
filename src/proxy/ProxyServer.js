const mc = require('minecraft-protocol');
const { createLogger } = require('../utils/logger');
const {
  wrapClientEnd,
  safeEndClient,
  disconnectServerClients,
  closeServerListenSocket,
} = require('../utils/clientDisconnect');
const { disableInboundChatValidation } = require('../utils/chatRelay');
const { installConfigurationJoin, rejectProxyLogin } = require('../utils/configReplay');

const log = createLogger('ProxyServer');

class ProxyServer {
  /**
   * @param {object} config
   * @param {(client: object) => void} onClientConnect
   * @param {import('../state/WorldStateCache').WorldStateCache} worldState
   * @param {() => { ok: boolean, reason?: string }} [canAcceptClient]
   * @param {(client: object) => Promise<void>} [preparePlayLogin]
   */
  constructor(config, onClientConnect, worldState, canAcceptClient, preparePlayLogin) {
    this.config = config;
    this.onClientConnect = onClientConnect;
    this.worldState = worldState;
    this.canAcceptClient = canAcceptClient;
    this.preparePlayLogin = preparePlayLogin;
    this.server = null;
    this.activeClient = null;
  }

  /** Clear the single-client slot if it belongs to this connection. */
  releaseClient(client) {
    if (this.activeClient === client) {
      this.activeClient = null;
    }
  }

  start() {
    this.server = mc.createServer({
      host: this.config.proxy.host || '0.0.0.0',
      'online-mode': this.config.proxy.onlineMode,
      // Java client chat is re-signed for the bot upstream; do not validate client signatures here.
      enforceSecureProfile: false,
      port: this.config.proxy.port,
      version: this.config.server.version,
      maxPlayers: this.config.proxy.maxClients,
      motd: '§6FlayerProxy',
      hideErrors: true,
      errorHandler: (client, err) => {
        log.error(`Client error (${client.username || 'unknown'}):`, err.message);
        safeEndClient(client, err);
      },
    });

    this.server.on('login', (client) => {
      wrapClientEnd(client);
      client.removeAllListeners('login_acknowledged');

      const slot = this.canAcceptClient?.() ?? {
        ok: !this.activeClient,
        reason: 'Only one client can connect at a time.',
      };
      if (!slot.ok || this.activeClient) {
        const reason = slot.reason || 'Only one client can connect at a time.';
        log.warn(`Rejecting login for ${client.username || 'client'}: ${reason}`);
        rejectProxyLogin(client, reason);
        return;
      }

      this.activeClient = client;
      client.on('end', () => {
        log.info(`Client disconnected: ${client.username}`);
        this.releaseClient(client);
      });

      // Register login_acknowledged before reconnect finishes — client sends ack immediately.
      installConfigurationJoin(client, this.server, this.worldState, log, {
        beforeConfigReplay: this.preparePlayLogin
          ? () => this.preparePlayLogin(client)
          : undefined,
      });
    });

    this.server.on('playerJoin', (client) => {
      disableInboundChatValidation(client);

      if (this.activeClient !== client) {
        log.warn(`Rejecting playerJoin for ${client.username} — not the active client`);
        rejectProxyLogin(client, 'Only one client can connect at a time.');
        return;
      }

      log.info(`Client ready: ${client.username}`);
      this.onClientConnect(client);
    });

    this.server.on('error', (err) => {
      log.error('Proxy server error:', err.message);
    });

    this.server.on('listening', () => {
      log.info(`Proxy server listening on port ${this.config.proxy.port}`);
    });
  }

  /**
   * Replace the registry codec sent to clients during configuration.
   * Must be called after the bot has received registry_data from the upstream server.
   * @param {object} codec
   */
  updateRegistryCodec(codec) {
    if (!this.server?.options) return;
    this.server.options.registryCodec = codec;
    const count = codec.codec ? 1 : Object.keys(codec).length;
    if (count === 0) {
      log.info('Proxy registry disabled (using raw upstream config packets)');
    } else {
      log.info(`Proxy registry updated from server (${count} registries)`);
    }
  }

  async stop() {
    await disconnectServerClients(this.server, 'Proxy shutting down');
    closeServerListenSocket(this.server);
    this.activeClient = null;
    this.server = null;
  }
}

module.exports = { ProxyServer };

const mc = require('minecraft-protocol');
const { createLogger } = require('../utils/logger');
const { wrapClientEnd, safeEndClient } = require('../utils/clientDisconnect');
const { disableInboundChatValidation } = require('../utils/chatRelay');
const { replayConfigToClient } = require('../utils/configReplay');

const log = createLogger('ProxyServer');

class ProxyServer {
  /**
   * @param {object} config
   * @param {(client: object) => void} onClientConnect
   * @param {import('../state/WorldStateCache').WorldStateCache} worldState
   * @param {() => { ok: boolean, reason?: string }} [canAcceptClient]
   */
  constructor(config, onClientConnect, worldState, canAcceptClient) {
    this.config = config;
    this.onClientConnect = onClientConnect;
    this.worldState = worldState;
    this.canAcceptClient = canAcceptClient;
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

      const slot = this.canAcceptClient?.() ?? {
        ok: !this.activeClient,
        reason: 'Only one client can connect at a time.',
      };
      if (!slot.ok || this.activeClient) {
        const reason = slot.reason || 'Only one client can connect at a time.';
        log.warn(`Rejecting login for ${client.username || 'client'}: ${reason}`);
        client.end(reason);
        return;
      }

      this.activeClient = client;
      client.on('end', () => {
        log.info(`Client disconnected: ${client.username}`);
        this.releaseClient(client);
      });

      client.prependOnceListener('login_acknowledged', () => {
        replayConfigToClient(client, this.worldState, log);
      });
    });

    this.server.on('playerJoin', (client) => {
      disableInboundChatValidation(client);

      if (this.activeClient !== client) {
        log.warn(`Rejecting playerJoin for ${client.username} — not the active client`);
        client.end('Only one client can connect at a time.');
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

  stop() {
    if (this.activeClient) {
      try { this.activeClient.end('Proxy shutting down'); } catch (e) {}
      this.activeClient = null;
    }
    if (this.server) {
      this.server.close();
    }
  }
}

module.exports = { ProxyServer };

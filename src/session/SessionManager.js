const { createLogger } = require('../utils/logger');
const { ServerConnection } = require('./ServerConnection');
const { ProxyServer } = require('../proxy/ProxyServer');
const { SpectatorProxyServer } = require('../proxy/SpectatorProxyServer');
const { SpectatorHub } = require('../spectator/SpectatorHub');
const { WorldStateCache } = require('../state/WorldStateCache');
const { StateReplayer } = require('../replay/StateReplayer');
const { performHandoff } = require('./handoffFlow');
const { removeHandoffUpstreamRelay } = require('../utils/handoffSync');
const { disconnectReasonText } = require('../utils/clientDisconnect');

const log = createLogger('Session');

/**
 * Session states
 */
const State = {
  INIT: 'INIT',
  BOT_MODE: 'BOT_MODE',
  HANDOFF: 'HANDOFF',
  CLIENT_MODE: 'CLIENT_MODE',
};

/**
 * Orchestrates the lifecycle: bot mode ↔ client mode.
 *
 * - INIT: Connecting to server
 * - BOT_MODE: No client connected, bot holds the session
 * - HANDOFF: Client just connected, replaying cached state
 * - CLIENT_MODE: Client is in control, packets piped bidirectionally
 */
class SessionManager {
  constructor(config) {
    this.config = config;
    this.state = State.INIT;
    this._shuttingDown = false;
    this._reconnectTimer = null;

    // Core components
    this.worldState = new WorldStateCache(config);
    this.serverConn = new ServerConnection(config, this.worldState);
    this.proxyServer = new ProxyServer(
      config,
      (client) => this._onClientConnect(client),
      this.worldState,
      () => this._clientSlotStatus(),
    );
    this.replayer = new StateReplayer(this.worldState, this.serverConn);

    if (config.spectator.enabled !== false) {
      this.spectatorHub = new SpectatorHub(this.serverConn, this.worldState, this.replayer, config);
      this.spectatorProxy = new SpectatorProxyServer(
        config,
        this.spectatorHub,
        this.worldState,
        () => this._spectatorSlotStatus(),
      );
    } else {
      this.spectatorHub = null;
      this.spectatorProxy = null;
    }

    // Current client bridge (if in CLIENT_MODE)
    this.clientBridge = null;
    this.currentClient = null;

    this._setupServerEvents();
  }

  /**
   * Boot up: connect to server and start proxy.
   */
  start() {
    log.info('Starting FlayerProxy...');
    this.serverConn.connect();
    this.proxyServer.start();
    if (this.spectatorProxy) {
      this.spectatorProxy.start();
    }
  }

  /**
   * Schedule a reconnect, cancelling any previous pending one.
   */
  _scheduleReconnect(delaySec) {
    if (this._shuttingDown) return;

    // Cancel any existing timer
    if (this._reconnectTimer) {
      clearTimeout(this._reconnectTimer);
      this._reconnectTimer = null;
    }

    log.info(`Reconnecting in ${delaySec} seconds...`);
    this._reconnectTimer = setTimeout(() => {
      this._reconnectTimer = null;
      if (this._shuttingDown) return;
      this.worldState.clear();
      this.serverConn.connect();
    }, delaySec * 1000);
  }

  /**
   * Setup event handlers for server connection lifecycle.
   */
  _setupServerEvents() {
    this.serverConn.on('connected', () => {
      log.info('Server connection established');
      // Empty codec: login.js only sends finish_configuration; full config order is replayed on join.
      if (this.worldState.isConfigReady()) {
        this.proxyServer.updateRegistryCodec({});
        this.spectatorProxy?.updateRegistryCodec({});
        const entries = this.worldState.getConfigReplayEntries();
        const registryCount = entries.filter((p) => p.name === 'registry_data').length;
        log.info(
          `Config replay ready (${entries.length} packets, ${registryCount} registries)`
        );
      } else {
        const registryCodec = this.worldState.buildRegistryCodec();
        if (registryCodec) {
          this.proxyServer.updateRegistryCodec(registryCodec);
          this.spectatorProxy?.updateRegistryCodec(registryCodec);
          log.info('Registry codec ready (parsed fallback, no ordered config capture)');
        } else {
          this.proxyServer.updateRegistryCodec({});
          this.spectatorProxy?.updateRegistryCodec({});
          log.warn('No registry_data captured from server — proxy clients will use minecraft-data defaults');
        }
      }
      this._transitionTo(State.BOT_MODE);
    });

    this.serverConn.on('disconnected', (reason) => {
      log.warn(`Server disconnected: ${reason}`);
      if (this.state === State.INIT) return; // Already handled by kicked

      // Kick any connected client
      if (this.currentClient) {
        try {
          this.currentClient.end(`Server disconnected: ${disconnectReasonText(reason)}`);
        } catch (e) { /* ignore */ }
      }

      this._cleanupClient();
      this.spectatorHub?.kickAll(`Server disconnected: ${disconnectReasonText(reason)}`);
      this._transitionTo(State.INIT);
      this._scheduleReconnect(5);
    });

    this.serverConn.on('kicked', (reason) => {
      log.error(`Kicked from server: ${JSON.stringify(reason)}`);

      if (this.currentClient) {
        try {
          this.currentClient.end(`Kicked from server: ${disconnectReasonText(reason)}`);
        } catch (e) { /* ignore */ }
      }

      this._cleanupClient();
      this.spectatorHub?.kickAll(`Kicked from server: ${disconnectReasonText(reason)}`);
      this._transitionTo(State.INIT);
      this._scheduleReconnect(15);
    });

    this.serverConn.on('error', (err) => {
      log.error(`Server error: ${err.message}`);
    });

    this.serverConn.on('death', () => {
      if (this.currentClient && this.state === State.CLIENT_MODE) {
        log.warn('Bot died while client is connected — will resync when bot respawns');
      }
    });

    this.serverConn.on('respawn', () => {
      if (this.currentClient && this.state === State.CLIENT_MODE) {
        this._refreshClientAfterBotRespawn().catch((err) => {
          log.error('Failed to refresh session after respawn:', err.message);
        });
      }
    });
  }

  /**
   * Ask the server for chunks at the bot's current position if the cache is empty there.
   */
  async _primeChunksNearBot() {
    const bot = this.serverConn.bot;
    if (!bot?.entity?.position) return;

    const cx = Math.floor(bot.entity.position.x / 16);
    const cz = Math.floor(bot.entity.position.z / 16);

    if (this.worldState.chunks.getChunksForReplay(cx, cz, 2).length > 0) {
      return;
    }

    log.info(`No cached chunks at bot (${cx}, ${cz}) — nudging server chunk loader...`);
    // Server has no serverbound view-center packet; movement triggers ChunkMap.move().
    this.serverConn.confirmServerPosition();

    const rawClient = this.serverConn.rawClient;
    if (!rawClient) return;

    await new Promise((resolve) => {
      const timeout = setTimeout(() => {
        rawClient.removeListener('packet', onPacket);
        resolve();
      }, 1500);

      const onPacket = (data, meta) => {
        if (meta.state !== 'play' || meta.name !== 'map_chunk') return;
        if (this.worldState.chunks.getChunksForReplay(cx, cz, 2).length > 0) {
          clearTimeout(timeout);
          rawClient.removeListener('packet', onPacket);
          log.info('Received chunks from server for handoff');
          resolve();
        }
      };

      rawClient.on('packet', onPacket);
    });
  }

  /**
   * Bot respawned on the same server connection while a client is attached.
   */
  async _refreshClientAfterBotRespawn() {
    const client = this.currentClient;
    if (!client) return;

    log.info('Bot respawned — resyncing client to new position');
    this.worldState.entities.clear();

    await this.serverConn.syncProxyClientPosition(client);
    this.serverConn.confirmServerPosition();

    if (this.clientBridge) {
      this.clientBridge._syncClientViewFromBot();
    }
  }

  /**
   * Whether a new Java client may take the single proxy slot.
   * @returns {{ ok: boolean, reason?: string }}
   */
  _clientSlotStatus() {
    if (this.currentClient || this.proxyServer.activeClient) {
      return { ok: false, reason: 'Only one client can connect at a time.' };
    }
    if (this.state === State.INIT) {
      return {
        ok: false,
        reason: 'Proxy is still connecting to the server. Try again in a moment.',
      };
    }
    if (this.state !== State.BOT_MODE) {
      return { ok: false, reason: 'Another client session is active.' };
    }
    return { ok: true };
  }

  _rejectClient(client, reason) {
    log.warn(`Rejecting ${client.username}: ${reason}`);
    client.end(reason);
    this.proxyServer.releaseClient(client);
  }

  /**
   * Spectators may join when the bot session is live and watchable.
   * @returns {{ ok: boolean, reason?: string }}
   */
  _spectatorSlotStatus() {
    if (!this.serverConn.connected) {
      return { ok: false, reason: 'Proxy is not connected to the server.' };
    }
    if (!this.worldState.isConfigReady()) {
      return {
        ok: false,
        reason: 'Server configuration is not ready. Try again in a moment.',
      };
    }
    if (this.state === State.INIT) {
      return {
        ok: false,
        reason: 'Proxy is still connecting to the server. Try again in a moment.',
      };
    }
    if (this.state === State.HANDOFF) {
      return { ok: false, reason: 'Session is handing off — try again in a moment.' };
    }
    if (this.state === State.CLIENT_MODE) {
      return { ok: true };
    }
    if (this.state === State.BOT_MODE && this.serverConn._botControlEnabled) {
      return { ok: true };
    }
    if (this.state === State.BOT_MODE) {
      return {
        ok: false,
        reason: 'A player is connected on the main proxy port. Spectate after they disconnect.',
      };
    }
    return { ok: false, reason: 'Spectator mode is unavailable.' };
  }

  /**
   * Handle a new Java client connection from the proxy server.
   */
  async _onClientConnect(client) {
    if (this.proxyServer.activeClient !== client) {
      this._rejectClient(client, 'Only one client can connect at a time.');
      return;
    }
    if (this.state !== State.BOT_MODE || this.currentClient) {
      this._rejectClient(client, 'Another client session is active.');
      return;
    }

    // BOT_MODE → HANDOFF
    log.info(`Client ${client.username} connected — starting handoff`);
    this._transitionTo(State.HANDOFF);
    this.currentClient = client;

    // Disable bot physics; keep mineflayer chunk_batch ack until the bridge takes over
    this.serverConn.setBotControl(false);

    // Handle client disconnect during handoff
    const onDisconnect = () => {
      log.info('Client disconnected during handoff');
      this._cleanupClient();
      this._transitionTo(State.BOT_MODE);
      this.serverConn.setBotControl(true);
    };
    client.once('end', onDisconnect);

    const result = await performHandoff({
      client,
      serverConn: this.serverConn,
      worldState: this.worldState,
      replayer: this.replayer,
      proxyServer: this.proxyServer,
      primeChunks: () => this._primeChunksNearBot(),
      isHandoffState: () => this.state === State.HANDOFF,
    });

    client.removeListener('end', onDisconnect);

    if (!result) {
      this._cleanupClient();
      this._transitionTo(State.BOT_MODE);
      this.serverConn.setBotControl(true);
      return;
    }

    this._transitionTo(State.CLIENT_MODE);
    this.clientBridge = result.bridge;

    // Handle client disconnect in client mode
    client.on('end', () => {
      log.info('Client disconnected — returning to bot mode');
      this._cleanupClient();
      this._transitionTo(State.BOT_MODE);
      this.serverConn.setBotControl(true);
    });
  }

  /**
   * Clean up client bridge and references.
   */
  _cleanupClient() {
    if (this.clientBridge) {
      this.clientBridge.stop();
      this.clientBridge = null;
    }
    if (this.currentClient) {
      this.proxyServer.releaseClient(this.currentClient);
    }
    this.currentClient = null;
  }

  /**
   * Transition to a new state.
   */
  _transitionTo(newState) {
    const oldState = this.state;
    this.state = newState;
    log.info(`State: ${oldState} → ${newState}`);

    if (newState === State.BOT_MODE) {
      const summary = this.worldState.getSummary();
      log.info(`Cache status: ${summary.chunks} chunks, ${summary.entities} entities, position: ${summary.hasPosition}`);
    }
  }

  /**
   * Gracefully shut down everything.
   */
  stop() {
    this._shuttingDown = true;
    if (this._reconnectTimer) {
      clearTimeout(this._reconnectTimer);
      this._reconnectTimer = null;
    }
    log.info('Shutting down FlayerProxy...');
    this._cleanupClient();
    this.spectatorHub?.stop();
    this.spectatorProxy?.stop();
    this.proxyServer.stop();
    this.serverConn.disconnect();
  }
}

module.exports = { SessionManager };

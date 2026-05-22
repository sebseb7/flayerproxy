const { createLogger } = require('../utils/logger');
const { ServerConnection } = require('./ServerConnection');
const { ProxyServer } = require('../proxy/ProxyServer');
const { SpectatorProxyServer } = require('../proxy/SpectatorProxyServer');
const { SpectatorHub } = require('../spectator/SpectatorHub');
const { WorldStateCache } = require('../state/WorldStateCache');
const { StateReplayer } = require('../replay/StateReplayer');
const { performHandoff } = require('./handoffFlow');
const { minChunksForHandoff } = require('../replay/replayHelpers');
const { removeHandoffUpstreamRelay } = require('../utils/handoffSync');
const {
  disconnectReasonText,
  buildDisconnectPayload,
  gracefulEndClient,
  disconnectServerClients,
  closeServerListenSocket,
} = require('../utils/clientDisconnect');

const log = createLogger('Session');

function formatAutoLogoutLabel(reason) {
  if (!reason) return 'unknown';
  if (reason === 'damage') return 'took damage';
  if (typeof reason === 'string' && reason.startsWith('player:')) {
    return `player ${reason.slice('player:'.length)}`;
  }
  return reason;
}

function autoLogoutSpectatorMsg(reason) {
  return `Bot Auto disconnected (${formatAutoLogoutLabel(reason)})`;
}

function autoLogoutPlayWaitMsg(reason) {
  return `Bot Auto disconnected (${formatAutoLogoutLabel(reason)}). Reconnecting…`;
}
const AUTO_LOGOUT_RECONNECT_TIMEOUT_MS = 90_000;
const CHUNK_PRIME_MS_DEFAULT = 1500;
const CHUNK_PRIME_MS_AFTER_AUTO_LOGOUT = 12_000;

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
    this._suppressReconnect = false;
    /** @type {string|null} Set after auto-logout until bot is back */
    this._autoLogoutReason = null;
    /** @type {string|null} Shown in play client chat on next handoff */
    this._pendingAutoLogoutNotice = null;
    /** @type {Promise<void>|null} */
    this._autoLogoutReconnectPromise = null;
    this._reconnectTimer = null;

    // Core components
    this.worldState = new WorldStateCache(config);
    this.serverConn = new ServerConnection(config, this.worldState);
    this.proxyServer = new ProxyServer(
      config,
      (client) => this._onClientConnect(client),
      this.worldState,
      () => this._clientSlotStatus(),
      (client) => this._preparePlayLogin(client),
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
      this._applyRegistryToProxies();
      this._transitionTo(State.BOT_MODE);
    });

    this.serverConn.on('autoLogout', (reason) => {
      log.warn(`Auto logout triggered: ${reason}`);
      this._autoLogoutReason = reason;
      this._suppressReconnect = true;
      this.spectatorHub?.kickAll(autoLogoutSpectatorMsg(reason));
      this.serverConn.disconnect();
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
      if (this._suppressReconnect) {
        this._suppressReconnect = false;
        log.info(
          `Auto logout (${formatAutoLogoutLabel(this._autoLogoutReason)}) — bot offline until a player connects on the play port to reconnect`,
        );
        return;
      }
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

  _applyRegistryToProxies() {
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
  }

  async _waitForBotSessionReady() {
    const deadline = Date.now() + AUTO_LOGOUT_RECONNECT_TIMEOUT_MS;
    while (Date.now() < deadline) {
      if (
        this.serverConn.connected &&
        this.worldState.isConfigReady() &&
        this.worldState.player.getState().loginPacket
      ) {
        return;
      }
      await new Promise((r) => setTimeout(r, 100));
    }
    throw new Error('Timed out waiting for bot session data (config/login)');
  }

  _connectBotAndWait() {
    if (this.serverConn.connected) return Promise.resolve();

    return new Promise((resolve, reject) => {
      const timeout = setTimeout(() => {
        cleanup();
        reject(new Error('Timed out waiting for bot to connect'));
      }, AUTO_LOGOUT_RECONNECT_TIMEOUT_MS);

      const cleanup = () => {
        clearTimeout(timeout);
        this.serverConn.removeListener('connected', onConnected);
        this.serverConn.removeListener('kicked', onKicked);
        this.serverConn.removeListener('error', onError);
      };

      const onConnected = () => {
        cleanup();
        resolve();
      };
      const onKicked = (reason) => {
        cleanup();
        reject(new Error(`Bot kicked while connecting: ${disconnectReasonText(reason)}`));
      };
      const onError = (err) => {
        cleanup();
        reject(err instanceof Error ? err : new Error(String(err)));
      };

      this.serverConn.once('connected', onConnected);
      this.serverConn.once('kicked', onKicked);
      this.serverConn.once('error', onError);
      this.serverConn.connect();
    });
  }

  /**
   * Reconnect upstream after auto-logout; cache config, login, and chunks before play handoff.
   */
  async _startAutoLogoutReconnect() {
    if (this._autoLogoutReconnectPromise) {
      return this._autoLogoutReconnectPromise;
    }

    this._autoLogoutReconnectPromise = (async () => {
      try {
        if (!this.serverConn.connected) {
          log.info('Play client connected — reconnecting bot after auto logout…');
          this.worldState.clear();
          await this._connectBotAndWait();
        }
        await this._waitForBotSessionReady();
        this._applyRegistryToProxies();
        await this._primeChunksNearBot(CHUNK_PRIME_MS_AFTER_AUTO_LOGOUT);
        if (this._autoLogoutReason) {
          this._pendingAutoLogoutNotice = this._autoLogoutReason;
        }
        log.info('Bot ready for play handoff after auto logout');
      } finally {
        this._autoLogoutReconnectPromise = null;
      }
    })();

    return this._autoLogoutReconnectPromise;
  }

  _chunkHandoffCounts() {
    const bot = this.serverConn.bot;
    if (!bot?.entity?.position) return null;

    const cx = Math.floor(bot.entity.position.x / 16);
    const cz = Math.floor(bot.entity.position.z / 16);
    const viewDistance = this.worldState.misc.viewDistance?.viewDistance
      ?? this.config.bot?.viewDistance
      ?? 10;
    const count = this.worldState.chunks.getChunksForReplay(cx, cz, viewDistance).length;
    const min = minChunksForHandoff(viewDistance);
    return { cx, cz, viewDistance, count, min };
  }

  /**
   * Ask the server for chunks at the bot's current position until enough are cached for replay.
   * @param {number} [waitMs]
   */
  async _primeChunksNearBot(waitMs = CHUNK_PRIME_MS_DEFAULT) {
    const stats = this._chunkHandoffCounts();
    if (!stats) return;

    const { cx, cz, viewDistance, min } = stats;
    let { count } = stats;

    if (count >= min) {
      log.info(`Handoff cache ready: ${count}/${min} chunks at (${cx}, ${cz})`);
      return;
    }

    log.info(`Priming chunks at (${cx}, ${cz}): ${count}/${min} cached — nudging server...`);
    this.serverConn.confirmServerPosition();

    const rawClient = this.serverConn.rawClient;
    if (!rawClient) return;

    await new Promise((resolve) => {
      const finish = (label) => {
        clearTimeout(timeout);
        rawClient.removeListener('packet', onPacket);
        count = this.worldState.chunks.getChunksForReplay(cx, cz, viewDistance).length;
        if (count >= min) {
          log.info(`Primed ${count}/${min} chunks for handoff (${label})`);
        } else if (count > 0) {
          log.warn(
            `Only ${count}/${min} chunks cached after ${waitMs}ms — terrain may load slowly after handoff`,
          );
        } else {
          log.warn(`No chunks cached after ${waitMs}ms — client may stay on Loading Terrain`);
        }
        resolve();
      };

      const timeout = setTimeout(() => finish('timeout'), waitMs);

      const onPacket = (data, meta) => {
        if (meta.state !== 'play' || meta.name !== 'map_chunk') return;
        count = this.worldState.chunks.getChunksForReplay(cx, cz, viewDistance).length;
        if (count >= min) {
          finish('ready');
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
   * Reconnect upstream when a play client joins after auto-logout (awaited inside login_acknowledged).
   */
  async _preparePlayLogin(client) {
    const awaiting =
      this._autoLogoutReason || this._pendingAutoLogoutNotice;
    if (!awaiting) return;

    if (!this.serverConn.connected) {
      log.info(`Play client ${client.username} joining — starting bot reconnect after auto logout`);
      await this._startAutoLogoutReconnect();
      return;
    }

    await this._primeChunksNearBot(CHUNK_PRIME_MS_AFTER_AUTO_LOGOUT);
  }

  _clientSlotStatus() {
    if (this.currentClient || this.proxyServer.activeClient) {
      return { ok: false, reason: 'Only one client can connect at a time.' };
    }

    const awaitingPlayAfterAutoLogout =
      this._autoLogoutReason || this._pendingAutoLogoutNotice;

    if (this._autoLogoutReconnectPromise) {
      return { ok: false, reason: autoLogoutPlayWaitMsg(this._autoLogoutReason) };
    }

    if (awaitingPlayAfterAutoLogout && !this.serverConn.connected) {
      return { ok: true };
    }

    if (!this.serverConn.connected) {
      return {
        ok: false,
        reason:
          this.state === State.INIT
            ? 'Reconnecting to server. Try again in a moment.'
            : 'Proxy is not connected to the server.',
      };
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
    const awaitingPlayAfterAutoLogout =
      this._autoLogoutReason || this._pendingAutoLogoutNotice || this._autoLogoutReconnectPromise;
    if (awaitingPlayAfterAutoLogout && !this.serverConn.connected) {
      return { ok: false, reason: autoLogoutSpectatorMsg(this._autoLogoutReason) };
    }
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

  _notifyPlayClientAutoLogoutReconnect(client, reason) {
    const text = `FlayerProxy: ${autoLogoutSpectatorMsg(reason)}. Reconnected — starting handoff…`;
    try {
      if (client.state === 'play' && !client.ended) {
        client.write('system_chat', {
          content: buildDisconnectPayload(client, text),
          isActionBar: false,
        });
      }
    } catch (err) {
      log.warn(`Could not send auto-logout notice to ${client.username}: ${err.message}`);
    }
  }

  /**
   * Handle a new Java client connection from the proxy server.
   */
  async _onClientConnect(client) {
    if (this.proxyServer.activeClient !== client) {
      this._rejectClient(client, 'Only one client can connect at a time.');
      return;
    }

    const afterAutoLogout = !!this._pendingAutoLogoutNotice;
    if (afterAutoLogout) {
      this._notifyPlayClientAutoLogoutReconnect(client, this._pendingAutoLogoutNotice);
      this._pendingAutoLogoutNotice = null;
      this._autoLogoutReason = null;
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
      primeChunks: () =>
        this._primeChunksNearBot(
          afterAutoLogout ? CHUNK_PRIME_MS_AFTER_AUTO_LOGOUT : CHUNK_PRIME_MS_DEFAULT,
        ),
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
   * Gracefully shut down everything (disconnect packets before closing listen sockets).
   */
  async stop() {
    this._shuttingDown = true;
    if (this._reconnectTimer) {
      clearTimeout(this._reconnectTimer);
      this._reconnectTimer = null;
    }
    log.info('Shutting down FlayerProxy...');
    this._autoLogoutReason = null;
    this._pendingAutoLogoutNotice = null;

    const shutdownReason = 'Proxy shutting down';
    this._cleanupClient();

    const disconnects = [];
    if (this.currentClient) {
      disconnects.push(gracefulEndClient(this.currentClient, shutdownReason));
    }
    if (this.spectatorHub) {
      for (const client of this.spectatorHub._spectators.keys()) {
        disconnects.push(gracefulEndClient(client, shutdownReason));
      }
      this.spectatorHub.stop();
    }

    await Promise.all(disconnects);

    if (this.spectatorProxy?.server) {
      await disconnectServerClients(this.spectatorProxy.server, shutdownReason);
      closeServerListenSocket(this.spectatorProxy.server);
      this.spectatorProxy.server = null;
    }
    if (this.proxyServer.server) {
      await disconnectServerClients(this.proxyServer.server, shutdownReason);
      closeServerListenSocket(this.proxyServer.server);
      this.proxyServer.activeClient = null;
      this.proxyServer.server = null;
    }

    this.serverConn.disconnect();
  }
}

module.exports = { SessionManager };

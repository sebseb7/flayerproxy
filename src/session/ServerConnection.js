const EventEmitter = require('events');
const mineflayer = require('mineflayer');
const { createLogger } = require('../utils/logger');
const {
  relayClientMovement,
  syncProxyClientPosition,
  confirmServerPosition,
  acceptGrimSetbackOnUpstream,
} = require('./MovementRelay');
const { ChunkAckManager } = require('./ChunkAckManager');
const { BotIdleBehavior } = require('./BotIdleBehavior');
const { BotAutoLogout } = require('./BotAutoLogout');
const {
  ANIMATION_SWING_MAIN_HAND,
  ANIMATION_SWING_OFF_HAND,
} = require('../constants/spectatorPackets');
const { buildPlayerPoseMetadata } = require('../utils/playerVisualRelay');
const { installTickEndRelay } = require('./tickEndRelay');
const { installUpstreamMovementLog } = require('../utils/upstreamMovementLog');
const {
  stashMineflayerPositionListeners,
  restoreMineflayerPositionListeners,
} = require('./mineflayerPositionGuard');

const log = createLogger('ServerConn');

/**
 * Manages the persistent connection to the Minecraft server via a Mineflayer bot.
 * Provides access to both the high-level bot API and the raw minecraft-protocol client.
 */
class ServerConnection extends EventEmitter {
  /**
   * @param {object} config - Full config object
   * @param {import('../state/WorldStateCache').WorldStateCache} worldState
   */
  constructor(config, worldState) {
    super();
    this.config = config;
    this.worldState = worldState;
    this.bot = null;
    this.rawClient = null;
    this.connected = false;
    this._botControlEnabled = true;
    /** True after first spawn; later spawns are respawns on the same connection */
    this._initialSpawnDone = false;
    this._chunkAck = new ChunkAckManager();
    /** @type {BotIdleBehavior|null} */
    this._idleBehavior = null;
    /** @type {BotAutoLogout|null} */
    this._autoLogout = null;
    /** @type {(() => void)|null} */
    this._tickEndCleanup = null;
    /** @type {(() => void)|null} */
    this._movementLogCleanup = null;
    this._movementHooks = {
      ghostBlocked: { counts: new Map() },
    };
    /** >0 while ClientBridge is writing to upstream (Java client packets) */
    this._bridgeRelayDepth = 0;
    /** Mirrors bot sneak for spectator camera height (position Y offset). */
    this.botSneaking = false;
  }

  /**
   * Connect the bot to the Minecraft server.
   */
  connect() {
    log.info(`Connecting to ${this.config.server.host}:${this.config.server.port} as ${this.config.auth.username}...`);
    this._initialSpawnDone = false;
    this._idleBehavior?.stop();
    this._idleBehavior = null;
    this._autoLogout?.stop();
    this._autoLogout = null;
    this._tickEndCleanup?.();
    this._tickEndCleanup = null;
    this._movementLogCleanup?.();
    this._movementLogCleanup = null;

    this.bot = mineflayer.createBot({
      host: this.config.server.host,
      port: this.config.server.port,
      username: this.config.auth.username,
      auth: this.config.auth.auth,
      version: this.config.server.version,
      viewDistance: this.config.bot.viewDistance,
      checkTimeoutInterval: 60000,
      hideErrors: false,
    });

    this.rawClient = this.bot._client;
    this._movementHooks.ghostBlocked.counts.clear();
    const hooks = {
      ghostBlocked: this._movementHooks.ghostBlocked,
      isBotMode: () => this._botControlEnabled && this._bridgeRelayDepth === 0,
      isBridgeRelay: () => this._bridgeRelayDepth > 0,
    };
    this._movementLogCleanup = installUpstreamMovementLog(this.rawClient, hooks, {
      enabled: this.config.bot.logMovement !== false,
    });
    this._tickEndCleanup = installTickEndRelay(this.bot, this.config.server.version, hooks);

    this._setupConfigCapture();
    this._setupPacketCapture();
    this._setupBotEvents();
  }

  /**
   * Capture configuration-phase packets for later replay.
   */
  _setupConfigCapture() {
    const { CONFIG_CAPTURE_NAMES } = require('../utils/configReplay');

    this.rawClient.on('packet', (data, meta, buffer) => {
      if (meta.state !== 'configuration') return;
      if (!CONFIG_CAPTURE_NAMES.has(meta.name)) return;

      this.worldState.handleConfigReplayPacket(meta.name, data, buffer);
    });
  }

  /**
   * Hook into raw packet events to feed the world state cache.
   */
  _setupPacketCapture() {
    this.rawClient.on('packet', (data, meta, buffer) => {
      if (meta.state !== 'play') return;

      // Feed every server->client play packet to the world state cache
      this.worldState.handleServerPacket(meta.name, data, buffer);

      // Forward to any connected client (include raw buffer for chunk packets)
      this.emit('serverPacket', meta.name, data, buffer);
    });
  }

  /**
   * Setup high-level bot events.
   */
  _setupBotEvents() {
    this.bot.on('spawn', () => {
      log.info('Bot spawned in world');
      this.connected = true;
      if (this.bot.entity?.id != null) {
        this.worldState.player.entityId = this.bot.entity.id;
      }
      if (!this._idleBehavior) {
        this._idleBehavior = new BotIdleBehavior(this.bot, this.config.bot, {
          onSwing: (hand) => this._emitBotSwingAnimation(hand),
          onSneakChange: (sneaking) => this.emitPlayerPoseVisual(sneaking),
        });
      }
      if (!this._autoLogout) {
        this._autoLogout = new BotAutoLogout(
          this.bot,
          this.config.bot,
          () => this._botControlEnabled,
          (reason) => this.emit('autoLogout', reason),
          this.config.auth.username,
        );
      }
      if (this._botControlEnabled) {
        this._idleBehavior.start();
        this._autoLogout.start();
      }
      if (!this._initialSpawnDone) {
        this._initialSpawnDone = true;
        this.emit('connected');
      } else {
        this.emit('respawn');
      }
    });

    this.bot.on('end', (reason) => {
      log.warn(`Bot disconnected: ${reason}`);
      this.connected = false;
      this._idleBehavior?.stop();
      this._autoLogout?.stop();
      this._tickEndCleanup?.();
      this._tickEndCleanup = null;
      this.emit('disconnected', reason);
    });

    this.bot.on('kicked', (reason) => {
      log.error(`Bot kicked: ${JSON.stringify(reason)}`);
      this.connected = false;
      this.emit('kicked', reason);
    });

    this.bot.on('error', (err) => {
      log.error(`Bot error: ${err.message}`);
      this.emit('error', err);
    });

    this.bot.on('death', () => {
      log.warn('Bot died');
      this.emit('death');
      if (this._botControlEnabled) {
        setTimeout(() => {
          try {
            this.bot.respawn();
          } catch (e) {
            log.error('Failed to respawn:', e.message);
          }
        }, 1000);
      }
    });

    this.bot.on('messagestr', (text, messageType) => {
      if (!this.connected || !text) return;
      const label =
        messageType === 'chat' ? 'Chat' :
        messageType === 'system' ? 'Server' :
        messageType === 'game_info' ? 'ActionBar' :
        messageType;
      const line = `[${label}] ${text}`;
      if (messageType === 'game_info') {
        log.debug(line);
      } else {
        log.info(line);
      }
    });
  }



  /**
   * Server usually does not echo the bot's own arm swing back on its connection.
   * Push a clientbound animation so spectators see idle swings.
   * @param {'left'|'right'} hand
   */
  _emitBotSwingAnimation(hand) {
    this.emitBotVisual('animation', {
      animation: hand === 'left' ? ANIMATION_SWING_OFF_HAND : ANIMATION_SWING_MAIN_HAND,
    });
  }

  /**
   * Synthetic S2C for spectators (swing, crouch, jump) — not echoed to the bot connection.
   * @param {string} name - minecraft-protocol packet name
   * @param {object} data
   */
  emitBotVisual(name, data) {
    const entityId = this.bot?.entity?.id ?? this.worldState.player.entityId;
    if (entityId == null) return;

    const payload = { ...data, entityId: data.entityId ?? entityId };
    if (name === 'entity_metadata') {
      this.worldState.player.handleEntityMetadata(payload);
      this.worldState.entities.handleEntityMetadata(payload);
    }

    this.emit('botVisual', name, payload);
  }

  /**
   * Sneak/crouch is not echoed on the bot connection; push entity_metadata for spectators.
   * @param {boolean} sneaking
   */
  emitPlayerPoseVisual(sneaking) {
    this.botSneaking = sneaking;
    const packet = buildPlayerPoseMetadata(this.worldState, this, sneaking);
    if (packet) this.emitBotVisual('entity_metadata', packet);
    this.emit('spectatorSneakChange', sneaking);
  }

  /**
   * Enable/disable bot AI control.
   * When disabled, the bot stops all autonomous behavior.
   */
  setBotControl(enabled) {
    this._botControlEnabled = enabled;
    if (enabled) {
      log.info('Bot control ENABLED (bot mode)');
      restoreMineflayerPositionListeners(this.rawClient);
      if (this.bot) this.bot.physicsEnabled = true;
      this._idleBehavior?.start();
      this._autoLogout?.start();
    } else {
      log.info('Bot control DISABLED (client taking over)');
      stashMineflayerPositionListeners(this.rawClient);
      this._idleBehavior?.stop();
      this._autoLogout?.stop();
      if (this.bot) {
        this.bot.physicsEnabled = false;
        try {
          this.bot.clearControlStates();
        } catch (e) { /* ignore */ }
      }
    }
  }

  /**
   * When true, the Java client forwards chunk_batch_received and mineflayer must not auto-ack.
   * When false, mineflayer acks batches on the bot connection (required during handoff/replay).
   */
  setClientDrivesChunkBatchAck(clientDrives) {
    this.setProxyClientChunkAck(!clientDrives);
  }

  /** Unblock PlayerChunkSender if a batch finished without an ack yet. */
  flushChunkBatchAck() {
    this._chunkAck.flush(this.rawClient);
  }

  /**
   * Re-send permission entity_status after /op or on handoff (PlayerList.sendPlayerPermissionLevel).
   */
  refreshProxyClientPermissions(client) {
    const status = this.worldState.player.permissionStatus;
    if (!status || !client) return false;
    try {
      client.write('entity_status', { ...status });
      return true;
    } catch (err) {
      log.error('Failed to refresh client permissions:', err.message);
      return false;
    }
  }

  /**
   * Snap the proxy client to the bot's current server-side position.
   * Call after replay and before enabling movement forwarding.
   */
  async syncProxyClientPosition(client) {
    return syncProxyClientPosition(this.bot, this.worldState, client, this);
  }

  /**
   * Tell the server the bot's current position (serverbound position_look).
   */
  confirmServerPosition() {
    this._bridgeRelayDepth += 1;
    try {
      return confirmServerPosition(this.bot, this.rawClient, this.connected);
    } finally {
      this._bridgeRelayDepth -= 1;
    }
  }

  /**
   * Serverbound settings — drives PlayerChunkSender / ChunkMap radius on the bot connection.
   * @param {number} viewDistance
   */
  applyUpstreamViewDistance(viewDistance) {
    if (!this.rawClient || !this.connected) return false;
    const vd = Math.max(2, Math.min(32, Math.floor(viewDistance)));
    try {
      this._bridgeRelayDepth += 1;
      this.rawClient.write('settings', {
        locale: 'en_US',
        viewDistance: vd,
        chatFlags: 0,
        chatColors: true,
        skinParts: 0xff,
        mainHand: 1,
        enableTextFiltering: false,
        enableServerListing: true,
        particleStatus: 0,
      });
      return true;
    } catch (err) {
      log.warn(`applyUpstreamViewDistance(${vd}) failed:`, err.message);
      return false;
    } finally {
      this._bridgeRelayDepth -= 1;
    }
  }

  /** Align upstream chunk radius with server / bot / java client (see viewDistance.js). */
  syncChunkStreamingViewDistance() {
    const ctx = this.worldState.getViewDistanceContext();
    const ok = this.applyUpstreamViewDistance(ctx.upstream);
    if (ok) {
      this.worldState.logViewDistanceContext('upstream sync');
    }
    return { ok, ...ctx };
  }

  /** Prompt the server to (re)send chunks (uses coordinated upstream view distance). */
  nudgeClientSettings() {
    const ctx = this.worldState.getViewDistanceContext();
    return this.applyUpstreamViewDistance(ctx.upstream);
  }

  /** Grim setback accept on upstream only — do not show to java client */
  acceptGrimSetback(data) {
    this._bridgeRelayDepth += 1;
    try {
      return acceptGrimSetbackOnUpstream(this.bot, this, data);
    } finally {
      this._bridgeRelayDepth -= 1;
    }
  }

  /**
   * While a Java client is connected, only the client should send chunk_batch_received
   * (see PlayerChunkSender.onChunkBatchReceivedByClient). Mineflayer auto-acks otherwise.
   */
  setProxyClientChunkAck(enabled) {
    if (enabled) {
      this._chunkAck.restore(this.rawClient);
    } else {
      this._chunkAck.disable(this.rawClient);
    }
  }

  /**
   * Apply a proxy client's movement packet to the bot entity, then send serverbound packets
   * using the client's coordinates so ChunkMap.move() tracks where the player walks.
   * @returns {boolean} false only when the bot entity is not ready
   */
  relayClientMovement(name, data, opts) {
    this._bridgeRelayDepth += 1;
    try {
      return relayClientMovement(this.bot, this.rawClient, name, data, {
        syncEntity: opts?.syncEntity ?? this._botControlEnabled,
      });
    } finally {
      this._bridgeRelayDepth -= 1;
    }
  }

  /**
   * Write a packet to the upstream server.
   * @param {string} name
   * @param {object} data
   * @param {{ source?: string }} [opts] - logged for handoff trace packets (player_loaded, etc.)
   */
  writeToServer(name, data, opts = {}) {
    if (!this.rawClient || !this.connected) return;
    if (opts.source) {
      const { logProxyC2S } = require('../utils/handoffTrace');
      logProxyC2S(log, name, data, opts.source);
    }
    this._bridgeRelayDepth += 1;
    try {
      this.rawClient.write(name, data);
    } finally {
      this._bridgeRelayDepth -= 1;
    }
  }

  /**
   * Gracefully close the connection.
   */
  disconnect() {
    this._idleBehavior?.stop();
    this._autoLogout?.stop();
    if (this.bot) {
      this.bot.quit();
    }
  }
}

module.exports = { ServerConnection };

const { createLogger } = require('../utils/logger');
const { RAW_FORWARD_PACKETS } = require('../constants/rawPackets');
const {
  SPECTATOR_ALLOWED_C2S,
  SPECTATOR_BLOCKED_S2C,
  SPECTATOR_MOVEMENT_C2S,
} = require('../constants/spectatorPackets');
const { shouldForwardWaypointToClient } = require('../utils/waypointRelay');
const {
  buildClientboundPositionPacket,
  chunkCoordsFromBlock,
  ensureClientViewIncludesChunk,
  updateClientViewPosition,
} = require('../utils/positionSync');
const { disableInboundChatValidation } = require('../utils/chatRelay');
const { applySpectatorSneakYOffset } = require('../utils/playerVisualRelay');
const { safeEndClient } = require('../utils/clientDisconnect');

const log = createLogger('Spectator');

/**
 * Fans upstream S2C packets to multiple spectator clients (watch-only).
 */
class SpectatorHub {
  /**
   * @param {import('../session/ServerConnection').ServerConnection} serverConn
   * @param {import('../state/WorldStateCache').WorldStateCache} worldState
   * @param {import('../replay/StateReplayer').StateReplayer} replayer
   * @param {object} config
   */
  constructor(serverConn, worldState, replayer, config) {
    this.serverConn = serverConn;
    this.worldState = worldState;
    this.replayer = replayer;
    this.config = config;
    this._botVisualHandler = (name, data) => this._forwardToSpectators(name, data, null);
    serverConn.on('botVisual', this._botVisualHandler);
    this._sneakChangeHandler = () => this._resnapAllSpectators();
    serverConn.on('spectatorSneakChange', this._sneakChangeHandler);
    /** @type {Map<object, { view: { chunkX: number|null, chunkZ: number|null }, teleportId: number }>} */
    this._spectators = new Map();
    this._serverHandler = null;
    this._snapInterval = null;
  }

  get count() {
    return this._spectators.size;
  }

  _maxSpectators() {
    return this.config.spectator?.maxClients ?? 20;
  }

  _getViewDistance() {
    return (
      this.worldState.misc.viewDistance?.viewDistance ??
      this.config.bot?.viewDistance ??
      10
    );
  }

  _botBlockCoords() {
    const pos = this.serverConn.bot?.entity?.position;
    if (!pos) return null;
    return { x: pos.x, z: pos.z };
  }

  async addSpectator(client) {
    if (this._spectators.size >= this._maxSpectators()) {
      client.end('Spectator slots are full.');
      return;
    }

    disableInboundChatValidation(client);

    const state = {
      view: { chunkX: null, chunkZ: null },
      teleportId: 0,
      knownWaypointKeys: new Set(this.worldState.misc.getKnownWaypointKeys()),
    };
    this._installClientGuard(client, state);

    await this.replayer.replaySpectator(client);

    this._spectators.set(client, state);
    this._syncViewFromBot(state.view);
    this._lockCamera(client);
    this._snapPosition(client, state);
    this._attachServerFanout();
    this._startSnapLoop();

    client.on('end', () => this.removeSpectator(client));
    log.info(`Spectator active: ${client.username} (${this._spectators.size} watching)`);
  }

  removeSpectator(client) {
    if (!this._spectators.delete(client)) return;
    log.info(`Spectator left: ${client.username} (${this._spectators.size} watching)`);
    if (this._spectators.size === 0) {
      this._detachServerFanout();
      this._stopSnapLoop();
    }
  }

  kickAll(reason) {
    for (const client of [...this._spectators.keys()]) {
      safeEndClient(client, reason);
    }
    this._spectators.clear();
    this._detachServerFanout();
  }

  stop() {
    if (this._botVisualHandler) {
      this.serverConn.removeListener('botVisual', this._botVisualHandler);
      this._botVisualHandler = null;
    }
    if (this._sneakChangeHandler) {
      this.serverConn.removeListener('spectatorSneakChange', this._sneakChangeHandler);
      this._sneakChangeHandler = null;
    }
    this._spectators.clear();
    this._detachServerFanout();
    this._stopSnapLoop();
  }

  _resnapAllSpectators() {
    for (const [client, state] of this._spectators) {
      if (client.ended || client.state !== 'play') continue;
      this._lockCamera(client);
      this._snapPosition(client, state);
    }
  }

  _installClientGuard(client, state) {
    client.prependListener('packet', (data, meta) => {
      if (meta.state !== 'play') return;
      if (SPECTATOR_ALLOWED_C2S.has(meta.name)) return;

      if (SPECTATOR_MOVEMENT_C2S.has(meta.name)) {
        this._lockCamera(client);
        this._snapPosition(client, state);
      }
    });
  }

  _lockCamera(client) {
    const entityId = this.worldState.player.entityId;
    if (entityId == null || client.ended || client.state !== 'play') return;
    try {
      client.write('camera', { cameraId: entityId });
    } catch (err) {
      log.debug(`camera lock failed for ${client.username}: ${err.message}`);
    }
  }

  _snapPosition(client, state) {
    const bot = this.serverConn.bot;
    if (!bot?.entity?.position || client.ended || client.state !== 'play') return;
    let packet = buildClientboundPositionPacket(bot, ++state.teleportId);
    if (!packet) return;
    packet = applySpectatorSneakYOffset(this.serverConn, 'position', packet);
    try {
      client.write('position', packet);
    } catch (err) {
      log.debug(`position snap failed for ${client.username}: ${err.message}`);
    }
  }

  _startSnapLoop() {
    if (this._snapInterval) return;
    this._snapInterval = setInterval(() => {
      if (this._spectators.size === 0) {
        this._stopSnapLoop();
        return;
      }
      for (const [client, state] of this._spectators) {
        if (client.ended || client.state !== 'play') continue;
        this._lockCamera(client);
        this._snapPosition(client, state);
      }
    }, 1000);
  }

  _stopSnapLoop() {
    if (!this._snapInterval) return;
    clearInterval(this._snapInterval);
    this._snapInterval = null;
  }

  _syncViewFromBot(view) {
    const block = this._botBlockCoords();
    if (!block) return;
    const { chunkX, chunkZ } = chunkCoordsFromBlock(block.x, block.z);
    view.chunkX = chunkX;
    view.chunkZ = chunkZ;
  }

  _attachServerFanout() {
    if (this._serverHandler) return;
    this._serverHandler = (name, data, buffer) => {
      this._forwardToSpectators(name, data, buffer);
    };
    this.serverConn.on('serverPacket', this._serverHandler);
  }

  _detachServerFanout() {
    if (!this._serverHandler) return;
    this.serverConn.removeListener('serverPacket', this._serverHandler);
    this._serverHandler = null;
  }

  _forwardToSpectators(name, data, buffer) {
    if (this._spectators.size === 0) return;
    if (SPECTATOR_BLOCKED_S2C.has(name)) return;

    const block = this._botBlockCoords();
    const viewDistance = this._getViewDistance();

    for (const [client, state] of this._spectators) {
      if (client.ended || client.state !== 'play') continue;
      if (name === 'tracked_waypoint' && !shouldForwardWaypointToClient(data, state.knownWaypointKeys)) {
        continue;
      }

      try {
        if (name === 'position' && data.x != null && data.z != null) {
          const { chunkX, chunkZ } = chunkCoordsFromBlock(data.x, data.z);
          updateClientViewPosition(client, chunkX, chunkZ, state.view);
          this._lockCamera(client);
        }

        if (name === 'update_view_position') {
          state.view.chunkX = data.chunkX;
          state.view.chunkZ = data.chunkZ;
        }

        if (name === 'map_chunk' && data.x != null && data.z != null && block) {
          ensureClientViewIncludesChunk(
            client,
            block.x,
            block.z,
            data.x,
            data.z,
            viewDistance,
            state.view
          );
        }

        const payload = applySpectatorSneakYOffset(this.serverConn, name, data);
        if (buffer && RAW_FORWARD_PACKETS.has(name)) {
          client.writeRaw(buffer);
        } else {
          client.write(name, payload);
        }
      } catch (err) {
        const level = name === 'entity_metadata' || name === 'animation' ? 'warn' : 'debug';
        log[level](`Spectator forward ${name} to ${client.username}: ${err.message}`);
      }
    }
  }
}

module.exports = { SpectatorHub };

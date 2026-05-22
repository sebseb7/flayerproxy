const { createLogger } = require('../utils/logger');
const { RAW_FORWARD_PACKETS } = require('../constants/rawPackets');
const { shouldForwardWaypointToClient } = require('../utils/waypointRelay');
const { relayPlayClientVisual } = require('../utils/playerVisualRelay');
const {
  CHAT_SESSION_PACKETS,
  disableInboundChatValidation,
  relayClientChatAsUpstream,
} = require('../utils/chatRelay');
const {
  chunkCoordsFromBlock,
  updateClientViewPosition,
  ensureClientViewIncludesChunk,
} = require('../utils/positionSync');

const log = createLogger('ClientBridge');

/**
 * Manages bidirectional packet forwarding between a connected Java client
 * and the upstream server connection.
 *
 * In ClientMode: client→server and server→client packets are piped through.
 * The WorldStateCache continues to be updated from server packets.
 */
class ClientBridge {
  /**
   * @param {object} client - The minecraft-protocol client from the proxy server
   * @param {import('../session/ServerConnection').ServerConnection} serverConn
   * @param {import('../state/WorldStateCache').WorldStateCache} worldState
   */
  constructor(client, serverConn, worldState) {
    this.client = client;
    this.serverConn = serverConn;
    this.worldState = worldState;
    this.active = false;

    this._clientPacketHandler = null;
    this._serverPacketHandler = null;
    this._clientEndHandler = null;

    /** UUIDs the client has seen via player_info add_player */
    this.knownPlayerUuids = new Set(worldState.misc.getKnownPlayerUuids());

    // Packets the mineflayer bot already handles on the upstream connection.
    // Forwarding them again from the proxy client causes duplicate responses and kicks.
    this._blockedClientPackets = new Set([
      'keep_alive',
      'teleport_confirm',
      'message_acknowledgement',
    ]);

    /** Must reach the server for chunk streaming (PlayerChunkSender / hasClientLoaded) */
    this._priorityClientPackets = new Set([
      'chunk_batch_received',
      'player_loaded',
    ]);

    /** Block movement until client matches server (set true after syncProxyClientPosition) */
    this._movementSynced = false;
    this._movementPackets = new Set([
      'position',
      'position_look',
      'look',
      'flying',
      'vehicle_move',
      'steer_vehicle',
      'paddle_boat',
    ]);

    this._blockedServerPackets = new Set();

    /** Locator waypoints the play client has seen (replay + live track) */
    this._knownWaypointKeys = new Set(worldState.misc.getKnownWaypointKeys());

    /** Proxy client view center — map_chunk outside this range is ignored by vanilla */
    this._clientView = { chunkX: null, chunkZ: null };
    /** Last block coords from client movement (for view center ahead of server) */
    this._lastClientBlock = { x: null, z: null };
    /** Play-client visual relay state (sneak/jump not echoed S2C to self) */
    this._playVisualRelay = { lastSneak: null, lastJump: false };
  }

  _getViewDistance() {
    return (
      this.worldState.misc.viewDistance?.viewDistance ??
      this.serverConn.config?.bot?.viewDistance ??
      10
    );
  }

  /**
   * ClientboundSetChunkCacheCenterPacket — vanilla ignores map_chunk outside this center.
   * Use the moving player's block coords (client packet), not a lagging bot read.
   */
  _syncClientViewFromBlockCoords(blockX, blockZ) {
    if (blockX == null || blockZ == null) return;
    const { chunkX, chunkZ } = chunkCoordsFromBlock(blockX, blockZ);
    updateClientViewPosition(this.client, chunkX, chunkZ, this._clientView);
    this._lastClientBlock.x = blockX;
    this._lastClientBlock.z = blockZ;
  }

  _syncClientViewFromBot() {
    const pos = this.serverConn.bot?.entity?.position;
    if (!pos) return;
    this._syncClientViewFromBlockCoords(pos.x, pos.z);
  }

  /** Block coords to anchor view center (client ahead of bot, else bot). */
  _playerBlockCoordsForView() {
    if (this._lastClientBlock.x != null) {
      return { x: this._lastClientBlock.x, z: this._lastClientBlock.z };
    }
    const pos = this.serverConn.bot?.entity?.position;
    if (!pos) return null;
    return { x: pos.x, z: pos.z };
  }

  /**
   * If a map_chunk would be outside the client's cache radius, send center first.
   * Matches server ChunkMap sending ClientboundSetChunkCacheCenterPacket before batches.
   */
  _ensureViewIncludesChunk(chunkX, chunkZ) {
    const player = this._playerBlockCoordsForView();
    if (!player) return;
    ensureClientViewIncludesChunk(
      this.client,
      player.x,
      player.z,
      chunkX,
      chunkZ,
      this._getViewDistance(),
      this._clientView
    );
  }

  /**
   * Allow client movement packets to reach the server.
   */
  enableMovement() {
    this._movementSynced = true;
    this._syncClientViewFromBot();
    log.info('Client movement forwarding enabled');
  }

  start() {
    if (this.active) return;
    this.active = true;

    this.serverConn.setClientDrivesChunkBatchAck(true);
    this.serverConn.flushChunkBatchAck();

    log.info('Client bridge started — forwarding packets');
    this._playVisualRelay = { lastSneak: null, lastJump: false };
    disableInboundChatValidation(this.client);

    // Client → Server
    this._clientPacketHandler = (data, meta) => {
      if (!this.active) return;
      if (meta.state !== 'play') return;
      if (this._blockedClientPackets.has(meta.name)) return;
      if (!this._movementSynced && this._movementPackets.has(meta.name)) return;

      try {
        if (this._movementPackets.has(meta.name)) {
          // Update view center from client coords before relay — server sends center on
          // chunk boundary (ChunkMap.applyChunkTrackingView) but map_chunk may arrive first.
          if (
            (meta.name === 'position' || meta.name === 'position_look' || meta.name === 'vehicle_move') &&
            data.x != null &&
            data.z != null
          ) {
            this._syncClientViewFromBlockCoords(data.x, data.z);
          }

          const ok = this.serverConn.relayClientMovement(meta.name, data);
          if (!ok) {
            this.serverConn.confirmServerPosition();
            this.serverConn.syncProxyClientPosition(this.client).catch(() => {});
          }
          return;
        }

        if (CHAT_SESSION_PACKETS.has(meta.name)) {
          if (meta.name === 'message_acknowledgement') {
            return;
          }
          relayClientChatAsUpstream(this.serverConn, meta.name, data, log);
          return;
        }

        if (meta.name === 'player_input' || meta.name === 'arm_animation') {
          relayPlayClientVisual(this.serverConn, meta.name, data, this._playVisualRelay);
          this.serverConn.writeToServer(meta.name, data);
          return;
        }

        if (this._priorityClientPackets.has(meta.name)) {
          this.serverConn.writeToServer(meta.name, data);
          return;
        }

        this.serverConn.writeToServer(meta.name, data);
      } catch (err) {
        log.error(`Error forwarding client→server packet '${meta.name}':`, err.message);
      }
    };
    // Run before minecraft-protocol server chat validation (registered at login).
    this.client.prependListener('packet', this._clientPacketHandler);

    // Server → Client
    this._serverPacketHandler = (name, data, buffer) => {
      if (!this.active) return;
      if (this._blockedServerPackets.has(name)) return;
      if (name === 'tracked_waypoint' && !shouldForwardWaypointToClient(data, this._knownWaypointKeys)) {
        return;
      }
      if (name === 'player_info' && !this._shouldForwardPlayerInfo(data)) return;

      if (name === 'position') {
        this._movementSynced = true;
        if (data.x != null && data.z != null) {
          this._syncClientViewFromBlockCoords(data.x, data.z);
        }
      }

      if (name === 'update_view_position') {
        this._clientView.chunkX = data.chunkX;
        this._clientView.chunkZ = data.chunkZ;
      }

      if (name === 'map_chunk' && data.x != null && data.z != null) {
        this._ensureViewIncludesChunk(data.x, data.z);
      }

      try {
        if (this.client.state !== 'play') return;

        // update_view_position must arrive before map_chunk in the same batch (ChunkMap.java)
        if (buffer && RAW_FORWARD_PACKETS.has(name)) {
          this.client.writeRaw(buffer);
          return;
        }
        this.client.write(name, data);
      } catch (err) {
        log.error(`Error forwarding server→client packet '${name}':`, err.message);
      }
    };
    this.serverConn.on('serverPacket', this._serverPacketHandler);

    // Client disconnect
    this._clientEndHandler = () => {
      log.info('Client connection ended');
      this.stop();
    };
    this.client.on('end', this._clientEndHandler);
  }

  /**
   * Forward player_info adds; skip latency-only updates for unknown UUIDs.
   */
  _shouldForwardPlayerInfo(data) {
    const action = data.action;
    const entries = data.data || [];

    if (action && typeof action === 'object' && action.add_player) {
      for (const entry of entries) {
        if (entry.uuid) this.knownPlayerUuids.add(entry.uuid);
      }
      return true;
    }

    if (entries.length === 0) return true;

    const allKnown = entries.every((e) => e.uuid && this.knownPlayerUuids.has(e.uuid));
    if (allKnown) return true;

    const anyKnown = entries.some((e) => e.uuid && this.knownPlayerUuids.has(e.uuid));
    if (anyKnown) return true;

    return false;
  }

  /**
   * Stop bridging and clean up listeners.
   */
  stop() {
    if (!this.active) return;
    this.active = false;

    this.serverConn.setClientDrivesChunkBatchAck(false);

    if (this._clientPacketHandler) {
      this.client.removeListener('packet', this._clientPacketHandler);
    }
    if (this._serverPacketHandler) {
      this.serverConn.removeListener('serverPacket', this._serverPacketHandler);
    }
    if (this._clientEndHandler) {
      this.client.removeListener('end', this._clientEndHandler);
    }

    log.info('Client bridge stopped');
  }
}

module.exports = { ClientBridge };

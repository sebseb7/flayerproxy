const net = require('net');
const { createLogger } = require('../utils/logger');

const log = createLogger('ChunkStream');

/** S2C play packets that change terrain, lighting, or block state. */
const STREAM_BLOCK_PACKETS = [
  'map_chunk',
  'update_light',
  'block_change',
  'multi_block_change',
  'tile_entity_data',
];

/** S2C play packets that create/update/destroy entities or their state. */
const STREAM_ENTITY_PACKETS = [
  'spawn_entity',
  'entity_metadata',
  'entity_equipment',
  'entity_effect',
  'remove_entity_effect',
  'entity_destroy',
  'set_passengers',
  'entity_teleport',
  'rel_entity_move',
  'entity_move_look',
  'entity_look',
  'sync_entity_position',
  'entity_velocity',
  'entity_head_rotation',
  'entity_update_attributes',
];

/** S2C play packets for the local player (position, health, mode, spawn). */
const STREAM_PLAYER_PACKETS = [
  'login',
  'position',
  'update_health',
  'experience',
  'abilities',
  'entity_status',
  'spawn_position',
  'difficulty',
  'respawn',
  'game_state_change',
];

/** S2C play packets for containers and hotbar/inventory sync. */
const STREAM_INVENTORY_PACKETS = [
  'window_items',
  'set_slot',
  'held_item_slot',
  'set_player_inventory',
  'set_cursor_item',
];

/** S2C play packets for time, view, border, tab list, scoreboard, recipes, etc. */
const STREAM_WORLD_PACKETS = [
  'update_time',
  'unload_chunk',
  'chunk_batch_start',
  'chunk_batch_finished',
  'initialize_world_border',
  'world_border_center',
  'world_border_size',
  'world_border_lerp_size',
  'world_border_warning_delay',
  'world_border_warning_reach',
  'simulation_distance',
  'update_view_distance',
  'update_view_position',
  'declare_commands',
  'player_info',
  'player_remove',
  'playerlist_header',
  'scoreboard_objective',
  'scoreboard_display_objective',
  'scoreboard_score',
  'reset_score',
  'teams',
  'boss_bar',
  'tracked_waypoint',
  'tags',
  'server_data',
  'update_recipes',
  'declare_recipes',
  'advancements',
  'recipe_book_add',
  'recipe_book_settings',
];

/** C2S play packets: client movement (minecraft-protocol names on the wire). */
const STREAM_CLIENT_MOVEMENT_PACKETS = [
  'position',
  'position_look',
  'look',
  'flying',
  'teleport_confirm',
];

/** Frame names sent to chunk_stream_receiver (avoid S2C `position` collision). */
const C2S_CHUNK_STREAM_NAME = {
  position: 'c2s_position',
  position_look: 'c2s_position_look',
  look: 'c2s_look',
  flying: 'c2s_flying',
  teleport_confirm: 'c2s_teleport_confirm',
};

const STREAM_C2S_PACKETS = new Set(STREAM_CLIENT_MOVEMENT_PACKETS);

/** S2C configuration packets (registry sync before play). */
const STREAM_CONFIG_PACKETS = [
  'registry_data',
  'feature_flags',
  'tags',
  'custom_payload',
  'reset_chat',
  'select_known_packs',
  'server_links',
  'code_of_conduct',
];

/** Forwarded to chunk_stream_receiver (length-prefixed framed wire). */
const STREAM_PACKETS = new Set([
  ...STREAM_BLOCK_PACKETS,
  ...STREAM_ENTITY_PACKETS,
  ...STREAM_PLAYER_PACKETS,
  ...STREAM_INVENTORY_PACKETS,
  ...STREAM_WORLD_PACKETS,
]);

const STREAM_CONFIG_PACKET_SET = new Set(STREAM_CONFIG_PACKETS);

/**
 * @param {string} metaName - minecraft-protocol C2S packet name
 * @returns {string|null}
 */
function c2sChunkStreamName(metaName) {
  return C2S_CHUNK_STREAM_NAME[metaName] ?? null;
}

/**
 * @param {object|false|null|undefined} chunkStream - config.sniffer.chunkStream
 * @returns {{ host: string, port: number }|null}
 */
function parseChunkStreamConfig(chunkStream) {
  if (!chunkStream || chunkStream === false) return null;
  if (typeof chunkStream === 'string') {
    const idx = chunkStream.lastIndexOf(':');
    if (idx <= 0) return null;
    const host = chunkStream.slice(0, idx);
    const port = Number(chunkStream.slice(idx + 1));
    if (!host || !Number.isFinite(port) || port <= 0) return null;
    return { host, port };
  }
  if (typeof chunkStream === 'object' && chunkStream.host != null && chunkStream.port != null) {
    const port = Number(chunkStream.port);
    if (!Number.isFinite(port) || port <= 0) return null;
    return { host: String(chunkStream.host), port };
  }
  return null;
}

/**
 * TCP sink for framed play packets:
 *   uint32 BE inner_len, uint16 BE name_len, packet name UTF-8, wire (id + payload)
 */
class ChunkStream {
  /**
   * @param {{ host: string, port: number }} target
   */
  constructor(target) {
    this.host = target.host;
    this.port = target.port;
    this._socket = null;
    this._connecting = false;
    this._closed = false;
    this._queued = [];
    this._sent = 0;
  }

  _ensureConnected() {
    if (this._closed || this._socket) return;
    if (this._connecting) return;
    this._connecting = true;
    const socket = net.connect({ host: this.host, port: this.port });
    socket.setNoDelay(true);
    socket.on('connect', () => {
      this._connecting = false;
      log.info(`Connected to chunk stream ${this.host}:${this.port}`);
      this._flushQueue();
    });
    socket.on('error', (err) => {
      this._connecting = false;
      if (!this._closed) {
        log.warn(`Chunk stream error (${this.host}:${this.port}): ${err.message}`);
      }
      this._dropSocket();
    });
    socket.on('close', () => {
      this._connecting = false;
      this._dropSocket();
    });
    this._socket = socket;
  }

  _dropSocket() {
    if (!this._socket) return;
    try {
      this._socket.destroy();
    } catch (_) {}
    this._socket = null;
  }

  _flushQueue() {
    if (!this._socket?.writable || this._queued.length === 0) return;
    for (const item of this._queued) {
      this._writeFrame(item.wire, item.name);
    }
    this._queued.length = 0;
  }

  _writeFrame(wireBuffer, packetName) {
    const nameBuf = Buffer.from(packetName, 'utf8');
    if (nameBuf.length === 0 || nameBuf.length > 65535) return;
    const innerLen = 2 + nameBuf.length + wireBuffer.length;
    const header = Buffer.alloc(6 + nameBuf.length);
    header.writeUInt32BE(innerLen, 0);
    header.writeUInt16BE(nameBuf.length, 4);
    nameBuf.copy(header, 6);
    this._socket.write(header);
    this._socket.write(wireBuffer);
    this._sent++;
  }

  /**
   * @param {Buffer} wireBuffer - encoded play frame (packet id + payload)
   * @param {string} packetName - minecraft-protocol meta.name
   */
  send(wireBuffer, packetName) {
    if (this._closed || !wireBuffer?.length || !packetName) return;
    if (!STREAM_PACKETS.has(packetName)) return;
    this._enqueue(wireBuffer, packetName);
  }

  /**
   * Forward a C2S play packet (client → server movement).
   * @param {Buffer} wireBuffer
   * @param {string} metaName - minecraft-protocol meta.name
   */
  sendC2s(wireBuffer, metaName) {
    if (this._closed || !wireBuffer?.length || !metaName) return;
    if (!STREAM_C2S_PACKETS.has(metaName)) return;
    const frameName = c2sChunkStreamName(metaName);
    if (!frameName) return;
    this._enqueue(wireBuffer, frameName);
  }

  _enqueue(wireBuffer, packetName) {
    if (this._socket?.writable) {
      this._writeFrame(wireBuffer, packetName);
      return;
    }
    this._queued.push({ wire: wireBuffer, name: packetName });
    if (this._queued.length > 512) this._queued.shift();
    this._ensureConnected();
  }

  close() {
    if (this._closed) return;
    this._closed = true;
    this._queued.length = 0;
    this._dropSocket();
    if (this._sent > 0) {
      log.info(`Chunk stream closed (${this.host}:${this.port}, ${this._sent} packet(s) sent)`);
    }
  }
}

module.exports = {
  ChunkStream,
  parseChunkStreamConfig,
  c2sChunkStreamName,
  STREAM_PACKETS,
  STREAM_C2S_PACKETS,
  STREAM_CLIENT_MOVEMENT_PACKETS,
  STREAM_BLOCK_PACKETS,
  STREAM_ENTITY_PACKETS,
  STREAM_PLAYER_PACKETS,
  STREAM_INVENTORY_PACKETS,
  STREAM_WORLD_PACKETS,
  STREAM_CONFIG_PACKETS,
  STREAM_CONFIG_PACKET_SET,
};

const net = require('net');
const { createLogger } = require('../utils/logger');

const log = createLogger('ChunkStream');

/** S2C play packets forwarded to chunk_stream_receiver (length-prefixed framed wire). */
const STREAM_PACKETS = new Set([
  'map_chunk',
  'entity_equipment',
  'entity_update_attributes',
  'set_passengers',
  'spawn_entity',
  'tile_entity_data',
]);

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

module.exports = { ChunkStream, parseChunkStreamConfig, STREAM_PACKETS };

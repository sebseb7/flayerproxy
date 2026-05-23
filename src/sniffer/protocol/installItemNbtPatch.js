'use strict';

const debug = require('debug')('minecraft-protocol');
const debugSkip = process.env.DEBUG_SKIP?.split(',') ?? [];
const states = require('minecraft-protocol/src/states');
const {
  createSnifferSerializer,
  createSnifferDeserializer,
} = require('./createSnifferProtocol');

/**
 * Sniffer protocol compile with tolerant item NBT. Does not change mineflayer / main proxy.
 *
 * @param {import('minecraft-protocol').Client} client
 */
function patchClientItemNbt(client) {
  if (!client || client._snifferNbtPatched) return;
  client._snifferNbtPatched = true;

  client.setSerializer = function snifferSetSerializer(state) {
    this.serializer = createSnifferSerializer({
      isServer: this.isServer,
      version: this.version,
      state,
      customPackets: this.customPackets,
    });
    this.deserializer = createSnifferDeserializer({
      isServer: this.isServer,
      version: this.version,
      state,
      customPackets: this.customPackets,
      noErrorLogging: this.hideErrors,
    });

    this.splitter.recognizeLegacyPing = state === states.HANDSHAKING;

    this.serializer.on('error', (e) => {
      let parts;
      if (e.field) {
        parts = e.field.split('.');
        parts.shift();
      } else {
        parts = [];
      }
      const serializerDirection = !this.isServer ? 'toServer' : 'toClient';
      e.field = [this.protocolState, serializerDirection].concat(parts).join('.');
      e.message = `Serialization error for ${e.field} : ${e.message}`;
      if (!this.compressor) {
        this.serializer.pipe(this.framer);
      } else {
        this.serializer.pipe(this.compressor);
      }
      this.emit('error', e);
    });

    this.deserializer.on('error', (e) => {
      let parts = [];
      if (e.field) {
        parts = e.field.split('.');
        parts.shift();
      }
      const deserializerDirection = this.isServer ? 'toServer' : 'toClient';
      e.field = [this.protocolState, deserializerDirection].concat(parts).join('.');
      e.message = e.buffer
        ? `Parse error for ${e.field} (${e.buffer.length} bytes, ${e.buffer.toString('hex').slice(0, 6)}...) : ${e.message}`
        : `Parse error for ${e.field}: ${e.message}`;
      if (!this.compressor) {
        this.splitter.pipe(this.deserializer);
      } else {
        this.decompressor.pipe(this.deserializer);
      }
      this.emit('error', e);
    });

    this._mcBundle = [];
    const emitPacket = (parsed) => {
      this.emit('packet', parsed.data, parsed.metadata, parsed.buffer, parsed.fullBuffer);
      this.emit(parsed.metadata.name, parsed.data, parsed.metadata);
      this.emit('raw.' + parsed.metadata.name, parsed.buffer, parsed.metadata);
      this.emit('raw', parsed.buffer, parsed.metadata);
    };
    this.deserializer.on('data', (parsed) => {
      parsed.metadata.name = parsed.data.name;
      parsed.data = parsed.data.params;
      parsed.metadata.state = state;
      if (debug.enabled && !debugSkip.includes(parsed.metadata.name)) {
        debug('read packet ' + state + '.' + parsed.metadata.name);
        const s = JSON.stringify(parsed.data, null, 2);
        debug(s && s.length > 10000 ? parsed.data : s);
      }
      if (this._hasBundlePacket && parsed.metadata.name === 'bundle_delimiter') {
        if (this._mcBundle.length) {
          this._mcBundle.forEach(emitPacket);
          emitPacket(parsed);
          this._mcBundle = [];
        } else {
          this._mcBundle.push(parsed);
        }
      } else if (this._mcBundle.length) {
        this._mcBundle.push(parsed);
        if (this._mcBundle.length > 32) {
          this._mcBundle.forEach(emitPacket);
          this._mcBundle = [];
          this._hasBundlePacket = false;
        }
      } else {
        emitPacket(parsed);
      }
    });
  };
}

module.exports = { patchClientItemNbt };

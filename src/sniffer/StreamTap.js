const mc = require('minecraft-protocol');
const { createSplitter } = require('minecraft-protocol/src/transforms/framing');
const { createDeserializer } = require('minecraft-protocol/src/transforms/serializer');

const states = mc.states;

const S2C_CONFIGURATION_PACKETS = new Set([
  'registry_data',
  'feature_flags',
  'tags',
  'finish_configuration',
  'custom_payload',
  'reset_chat',
  'code_of_conduct',
  'server_data',
]);

/**
 * Parse-only tap: frames are split and parsed for logs; bytes are not modified.
 */
class StreamTap {
  constructor(dir, version, packetLog, hooks = {}) {
    this.dir = dir;
    this.version = version;
    this.packetLog = packetLog;
    this.hooks = hooks;
    this.session = hooks.session || {
      state: states.HANDSHAKING,
      compressionThreshold: -1,
      encrypted: false,
    };

    this.state = this.session.state;
    this.stats = hooks.stats;

    this.splitter = createSplitter();
    this._parsers = new Map();
    this.splitter.on('data', (frame) => this._onFrame(frame));
  }

  feed(chunk) {
    if (this.session.encrypted) {
      this.stats.encryptedBytes[this.dir] += chunk.length;
      this.stats.encryptedChunks[this.dir] += 1;
      this.packetLog.logOpaque(this.dir, chunk.length, { encrypted: true });
      return;
    }
    this.stats.rawBytes[this.dir] += chunk.length;
    this.splitter.write(chunk);
  }

  _syncState() {
    this.state = this.session.state;
  }

  _parser() {
    this._syncState();
    const key = `${this.state}:${this.dir}`;
    if (!this._parsers.has(key)) {
      this._parsers.set(
        key,
        createDeserializer({
          state: this.state,
          isServer: this.dir === 'C2S',
          version: this.version,
          noErrorLogging: true,
        }),
      );
    }
    return this._parsers.get(key);
  }

  _parseFrame(frame) {
    const payload =
      this.session.compressionThreshold >= 0 ? this._decompressFrame(frame) : frame;
    return this._parser().parsePacketBuffer(payload);
  }

  _decompressFrame(frame) {
    const { readVarInt } = require('protodef').types.varint;
    const zlib = require('zlib');
    const { value, size } = readVarInt(frame, 0);
    if (value === 0) return frame.slice(size);
    return zlib.inflateSync(frame.slice(size), { finishFlush: 2 });
  }

  _onFrame(frame) {
    if (this.session.encrypted) return;

    this.stats.frames[this.dir] += 1;

    let parsed;
    try {
      parsed = this._parseFrame(frame);
    } catch (err) {
      this.stats.parseErrors[this.dir] += 1;
      this.packetLog.logUnparsed(this.dir, this.state, frame, err.message);
      return;
    }
    if (!parsed) return;

    const name = parsed.data.name;
    const data = parsed.data.params;
    this.stats.packets[this.dir] += 1;

    this.packetLog.logPacket(this.dir, { state: this.state, name }, data, frame, {
      forwarded: 'tcp',
    });

    if (name === 'login_start' && data.username) {
      this.hooks.onUsername?.(data.username);
    }
    if (name === 'set_protocol' && data.serverHost) {
      const mode = data.nextState === 1 ? 'status_ping' : data.nextState === 2 ? 'login' : `nextState_${data.nextState}`;
      this.packetLog.writeMeta({
        type: 'handshake_intent',
        mode,
        serverHost: data.serverHost,
        serverPort: data.serverPort,
        protocolVersion: data.protocolVersion,
      });
    }
    if ((name === 'set_compression' || name === 'compress') && data.threshold != null) {
      this.session.compressionThreshold = data.threshold;
      this._parsers.clear();
      this.packetLog.writeMeta({ type: 'compression', threshold: data.threshold, dir: this.dir });
    }
    // Encryption starts after the client sends its encryption response (C2S), not on S2C offer.
    if (this.dir === 'C2S' && name === 'encryption_begin') {
      this.session.encrypted = true;
      this.packetLog.writeMeta({ type: 'encryption_started', dir: this.dir });
    }

    this._advanceState(name, data);
  }

  _setState(next) {
    if (next === this.session.state) return;
    this.session.state = next;
    this.state = next;
    this._parsers.clear();
  }

  _advanceState(name, data) {
    if (this.dir === 'C2S') {
      if (this.state === states.HANDSHAKING && name === 'set_protocol') {
        this._setState(data.nextState === 1 ? states.STATUS : states.LOGIN);
      } else if (this.state === states.LOGIN && name === 'login_acknowledged') {
        this._setState(states.CONFIGURATION);
      } else if (this.state === states.CONFIGURATION && name === 'finish_configuration') {
        this._setState(states.PLAY);
      }
      return;
    }

    if (this.state === states.LOGIN && S2C_CONFIGURATION_PACKETS.has(name)) {
      this._setState(states.CONFIGURATION);
    } else if (this.state === states.CONFIGURATION && name === 'finish_configuration') {
      this._setState(states.PLAY);
    }
  }
}

module.exports = { StreamTap };

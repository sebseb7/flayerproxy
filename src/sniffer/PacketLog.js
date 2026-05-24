const fs = require('fs');
const path = require('path');
const { formatTraceLine } = require('./packetTrace');
const {
  buildFullPacketPayload: buildFullPacketRecord,
  formatTracePayload,
} = require('./packetPayload');
const { createLogger } = require('../utils/logger');
const { resolvePacketName } = require('./packetMeta');
const { ENTITY_PACKETS } = require('./SnifferWorldCapture');
const pktConsole = createLogger('PktTrace');

/** JSONL lines longer than this go to sessionDir/line-NNNNNN.jsonl with a short ref in the main log. */
const MAX_INLINE_LINE = 400;

const LARGE_PACKETS = new Set([
  'map_chunk',
  'chunk_data',
  'level_chunk_with_light',
  'light_update',
  'custom_payload',
]);

/** Raw wire dumps (chunkLog): one file per packet under chunkLogDir/<sessionId>/. */
const CHUNK_PACKETS = new Set([
  'map_chunk',
  'chunk_data',
  'level_chunk_with_light',
  'light_update',
  'update_light',
  'unload_chunk',
  'chunk_batch_start',
  'chunk_batch_finished',
  'chunk_batch_received',
  'block_change',
  'multi_block_change',
  'tile_entity_data',
]);

/** Configuration-phase S2C packets stored when chunkLog is on (state-id registry for decode). */
const CHUNK_DECODE_PACKETS = new Set(['registry_data']);

function shouldChunkRawLog(chunkLog, chunkLogEntities, packetName) {
  if (!chunkLog) return false;
  if (CHUNK_PACKETS.has(packetName)) return true;
  if (CHUNK_DECODE_PACKETS.has(packetName)) return true;
  if (chunkLogEntities && ENTITY_PACKETS.has(packetName)) return true;
  return false;
}

/**
 * Append-only JSONL packet log for login / handoff analysis.
 */
class PacketLog {
  /**
   * @param {object} opts
   * @param {string} opts.logDir
   * @param {string} [opts.chunkLogDir] - defaults to logDir/chunks
   * @param {string} opts.sessionId
   * @param {boolean} [opts.includePayload=true]
   */
  constructor(opts) {
    this.includePayload = opts.includePayload !== false;
    this.logEveryPacket = opts.logEveryPacket !== false;
    this.traceLog =
      opts.traceLog === true ||
      (opts.traceLog !== false && this.logEveryPacket);
    this.sessionLog =
      opts.sessionLog === true || (opts.sessionLog !== false && this.logEveryPacket);
    this.chunkLog =
      opts.chunkLog === true || (opts.chunkLog !== false && this.logEveryPacket);
    this.chunkLogEntities = opts.chunkLogEntities === true;
    this.consolePacketLog = opts.consolePacketLog !== false;
    this.tracePayloadMaxLen = opts.tracePayloadMaxLen ?? 8192;
    this.version = opts.version ?? null;
    this.sessionId = opts.sessionId;
    this._seq = 0;
    this._traceSeq = 0;

    const dir = path.resolve(opts.logDir);
    fs.mkdirSync(dir, { recursive: true });
    const file = path.join(dir, `${opts.sessionId}.jsonl`);
    this.filePath = this.sessionLog ? file : null;
    this._spillDir = path.join(dir, opts.sessionId);
    if (this.sessionLog) {
      fs.mkdirSync(this._spillDir, { recursive: true });
    }
    this._spillCount = 0;
    this._stream = this.sessionLog ? fs.createWriteStream(file, { flags: 'a' }) : null;
    const traceFile = path.join(dir, `${opts.sessionId}.trace.log`);
    this.traceFilePath = this.traceLog ? traceFile : null;
    this._traceStream = this.traceLog
      ? fs.createWriteStream(traceFile, { flags: 'a' })
      : null;
    this._closed = false;
    this.spillDir = this.sessionLog ? this._spillDir : null;

    const chunkDir = path.resolve(opts.chunkLogDir ?? path.join(dir, 'chunks'));
    this._blockUpdateRawDir = this.chunkLog ? path.join(chunkDir, opts.sessionId) : null;
    if (this._blockUpdateRawDir) {
      fs.mkdirSync(this._blockUpdateRawDir, { recursive: true });
      if (opts.version || opts.server) {
        const info = {
          sessionId: opts.sessionId,
          version: opts.version ?? null,
          server: opts.server ?? null,
        };
        fs.writeFileSync(
          path.join(this._blockUpdateRawDir, 'session-info.json'),
          `${JSON.stringify(info, null, 2)}\n`,
        );
      }
    }
    this._blockUpdateRawSeq = 0;
    this._lastBlockUpdateRawMs = null;
    this._pendingTraceDecodedFile = null;
    this._pendingTracePacketName = null;

    const sessionStart = {
      type: 'session_start',
      sessionId: opts.sessionId,
      clientUsername: opts.clientUsername,
      server: opts.server,
      version: opts.version,
    };
    this.writeMeta(sessionStart);
  }

  writeMeta(record) {
    const row = { ...record, t: new Date().toISOString() };
    if (this.sessionLog) this._write(row);
    const { traceMeta } = require('./packetTrace');
    traceMeta(this, record);
  }

  /**
   * @param {'C2S'|'S2C'} dir
   * @param {object} meta - minecraft-protocol packet meta
   * @param {object} data - parsed params
   * @param {Buffer} [rawBuffer]
   * @param {object} [extra]
   */
  logUnparsed(dir, state, frame, message) {
    this._write({
      type: 'parse_error',
      seq: ++this._seq,
      t: new Date().toISOString(),
      dir,
      state,
      frameBytes: frame.length,
      headHex: frame.subarray(0, Math.min(16, frame.length)).toString('hex'),
      error: message,
      forwarded: 'tcp',
    });
  }

  /**
   * minecraft-protocol deserializer failure (unparsed wire — connection may continue).
   * @param {'C2S'|'S2C'} dir
   * @param {string} state
   * @param {Error} err - often has .field and .buffer
   */
  logDeserializerError(dir, state, err, extra = {}) {
    const frame = err.buffer;
    const entry = {
      type: 'parse_error',
      seq: ++this._seq,
      t: new Date().toISOString(),
      dir,
      leg: extra.leg ?? (dir === 'C2S' ? 'java' : 'backend'),
      state,
      unknown: true,
      field: err.field ?? null,
      error: err.message,
      forwarded: extra.forwarded ?? 'decode_failed',
    };
    if (frame?.length) {
      entry.frameBytes = frame.length;
      entry.headHex = frame.subarray(0, Math.min(64, frame.length)).toString('hex');
      const decodedFile = this._maybeDumpDecodedPacket(
        entry.seq,
        { name: 'parse_error', packetId: null, unknown: true, displayName: 'parse_error', note: entry.field },
        { field: entry.field, error: entry.error, params: null },
        frame,
      );
      if (decodedFile) entry.decodedFile = decodedFile;
    }
    if (this.logEveryPacket) {
      this._write(entry);
    }
    if (this.traceLog) {
      this._recordTrace({
        type: 'trace',
        event: 'parse_error',
        t: entry.t,
        leg: entry.leg,
        dir,
        state,
        name: 'parse_error',
        rawBytes: frame?.length,
        action: entry.forwarded,
        note: entry.field ?? entry.error,
      });
    }
  }

  logOpaque(dir, bytes, extra = {}) {
    if (extra.encrypted) {
      this._encryptedOpaque = this._encryptedOpaque || { C2S: 0, S2C: 0, bytes: { C2S: 0, S2C: 0 } };
      this._encryptedOpaque[dir]++;
      this._encryptedOpaque.bytes[dir] += bytes;
      const n = this._encryptedOpaque[dir];
      if (n !== 1 && n !== 5 && n % 100 !== 0) return;
      this._write({
        type: 'opaque_summary',
        seq: ++this._seq,
        t: new Date().toISOString(),
        dir,
        encryptedChunks: n,
        encryptedBytes: this._encryptedOpaque.bytes[dir],
        forwarded: 'tcp',
        note: 'Encrypted play traffic (map_chunk etc.) is forwarded but not decoded on a transparent pipe.',
      });
      return;
    }
    this._write({
      type: 'opaque',
      seq: ++this._seq,
      t: new Date().toISOString(),
      dir,
      bytes,
      forwarded: 'tcp',
      ...extra,
    });
  }

  /**
   * @param {'C2S'|'S2C'} dir
   * @param {'java'|'backend'} [leg] - which TCP/session leg was decoded
   */
  logPacket(dir, meta, data, rawBuffer, extra = {}) {
    if (!this.logEveryPacket && !this.sessionLog && !this.chunkLog) return;

    const leg = extra.leg ?? (dir === 'C2S' ? 'java' : 'backend');
    const nameInfo = resolvePacketName(meta, extra);
    const entry = {
      type: 'packet',
      seq: ++this._seq,
      t: new Date().toISOString(),
      leg,
      dir,
      state: meta.state,
      name: nameInfo.name,
      clientState: extra.clientState,
      upstreamState: extra.upstreamState,
      forwarded: extra.forwarded ?? extra.action ?? null,
      action: extra.action ?? extra.forwarded ?? null,
    };
    if (nameInfo.packetId != null) entry.packetId = nameInfo.packetId;
    if (nameInfo.unknown) entry.unknown = true;
    if (nameInfo.note) entry.note = extra.note ? `${nameInfo.note}; ${extra.note}` : nameInfo.note;

    const traceName = nameInfo.displayName;

    if (rawBuffer) {
      entry.rawBytes = rawBuffer.length;
    }

    const packetName = meta.name;
    const decodedFile = this._maybeDumpDecodedPacket(entry.seq, nameInfo, data, rawBuffer);
    if (decodedFile) {
      entry.decodedFile = decodedFile;
      this._pendingTraceDecodedFile = decodedFile;
    }
    this._pendingTracePacketName = traceName;

    if (this.includePayload) {
      const full = buildFullPacketRecord(data, rawBuffer, decodedFile);
      entry.data = full.params;
      if (full.wire) entry.wire = full.wire;
      else if (full.wireBytes != null) entry.wireBytes = full.wireBytes;
    }

    const skipTrace = entry.action === 'relay';
    if (this.traceLog && !skipTrace) {
      this._recordTrace({
        type: 'trace',
        event: 'rx',
        t: entry.t,
        leg,
        dir,
        state: meta.state,
        name: traceName,
        rawBytes: rawBuffer?.length,
        action: entry.action,
        note: extra.note,
        payload: this.includePayload
          ? formatTracePayload(
              buildFullPacketRecord(data, rawBuffer, decodedFile),
              this.tracePayloadMaxLen,
            )
          : null,
      });
    }

    if (shouldChunkRawLog(this.chunkLog, this.chunkLogEntities, packetName)) {
      this._writeChunkPacketRaw(packetName, rawBuffer);
    }

    if (!this.logEveryPacket) return;

    this._write(entry);
  }

  /** Picked up by traceRelay after logPacket for the same wire frame. */
  consumePendingDecodedFile() {
    const f = this._pendingTraceDecodedFile;
    this._pendingTraceDecodedFile = null;
    return f;
  }

  consumePendingTracePacketName() {
    const n = this._pendingTracePacketName;
    this._pendingTracePacketName = null;
    return n;
  }

  /** Reserved for optional decoded artifacts in the session log. */
  _maybeDumpDecodedPacket(_seq, _nameInfo, _data, _rawBuffer) {
    return null;
  }

  /** Write raw play-state wire bytes: chunkLogDir/<sessionId>/<timestamp>-<packetName> */
  _writeChunkPacketRaw(packetName, rawBuffer) {
    if (this._closed || !this._blockUpdateRawDir || !rawBuffer?.length) return;
    const ms = Date.now();
    if (ms === this._lastBlockUpdateRawMs) {
      this._blockUpdateRawSeq++;
    } else {
      this._lastBlockUpdateRawMs = ms;
      this._blockUpdateRawSeq = 0;
    }
    const fileName =
      this._blockUpdateRawSeq === 0
        ? `${ms}-${packetName}`
        : `${ms}-${packetName}-${this._blockUpdateRawSeq}`;
    fs.writeFileSync(path.join(this._blockUpdateRawDir, fileName), rawBuffer);
  }

  /** Full packet payload for trace lines (params + wire + decodedFile). */
  buildFullPacketPayload(data, rawBuffer, decodedFile) {
    if (!this.includePayload) return null;
    return buildFullPacketRecord(data, rawBuffer, decodedFile);
  }

  /** Plain-text + optional console line for every packet/bridge (no spill). */
  _recordTrace(e) {
    if (this._closed || !this._traceStream) return;
    const seq = e.seq ?? ++this._traceSeq;
    if (e.seq == null) e.seq = seq;
    if (!e.t) e.t = new Date().toISOString();
    const line = formatTraceLine(e);
    this._traceStream.write(`${line}\n`);
    if (this.consolePacketLog) {
      pktConsole.info(line);
    }
  }

  close(reason) {
    if (this._closed) return;
    const end = { type: 'session_end', reason: reason || 'closed' };
    this.writeMeta(end);
    this._closed = true;
    this._stream?.end();
    this._traceStream?.end();
  }

  _write(obj) {
    if (this._closed || !this.sessionLog || !this._stream) return;
    this._emitLine(obj, {
      spillDir: this._spillDir,
      spillCountRef: () => ++this._spillCount,
      writeRaw: (line) => this._writeRaw(line),
    });
  }

  _emitLine(obj, { spillDir, spillCountRef, writeRaw }) {
    const line = JSON.stringify(obj);
    if (line.length + 1 <= MAX_INLINE_LINE) {
      writeRaw(line);
      return;
    }
    const spillFile = `line-${String(spillCountRef()).padStart(6, '0')}.jsonl`;
    fs.writeFileSync(path.join(spillDir, spillFile), `${line}\n`);
    writeRaw(JSON.stringify(buildSpillRef(obj, spillFile)));
  }

  _writeRaw(line) {
    if (!this._stream) return;
    this._stream.write(`${line}\n`);
  }

}

function buildPreview(obj) {
  if (obj.type === 'packet') {
    const parts = [obj.leg, obj.dir, obj.state, obj.name].filter(Boolean).join(' ');
    const extra = obj.rawBytes != null ? ` ${obj.rawBytes}b` : '';
    const fwd = obj.forwarded ? ` →${obj.forwarded}` : '';
    return `${parts}${extra}${fwd}`.trim();
  }
  if (obj.type === 'session_start') {
    return `${obj.clientUsername ?? '?'} → ${obj.server ?? '?'}`;
  }
  if (obj.wire?.length != null) return `wire ${obj.wire.length}b (inline)`;
  if (obj.wireBytes != null) return `wire ${obj.wireBytes}b`;
  if (obj.decodedFile) return obj.decodedFile;
  if (obj.reason) return String(obj.reason);
  if (obj.username) return String(obj.username);
  const compact = JSON.stringify(obj.data ?? obj);
  return compact.length > 48 ? `${compact.slice(0, 45)}…` : compact;
}

/** Compact pointer + preview kept in the main log (≤ MAX_INLINE_LINE). */
function buildSpillRef(obj, spillFile) {
  const ref = { _spill: spillFile, type: obj.type };
  if (obj.seq != null) ref.seq = obj.seq;
  ref.preview = buildPreview(obj);
  let encoded = JSON.stringify(ref);
  while (encoded.length > MAX_INLINE_LINE && ref.preview.length > 6) {
    ref.preview = `${ref.preview.slice(0, ref.preview.length - 3)}…`;
    encoded = JSON.stringify(ref);
  }
  return ref;
}

module.exports = {
  PacketLog,
  CHUNK_PACKETS,
  CHUNK_DECODE_PACKETS,
  shouldChunkRawLog,
  LARGE_PACKETS,
  ENTITY_PACKETS,
};

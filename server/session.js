import fs from 'node:fs';
import path from 'node:path';
import chalk from 'chalk';
import { HS_LOGIN, LOGIN, CFG, PLAY, LOG_LEVELS } from '../client/constants.js';
const BUNDLE_DELIMITER = Buffer.alloc(0); // packet id 0x00, empty payload
import { createRequire } from 'node:module';
const _require = createRequire(import.meta.url);
const _playS2cById = _require('../libchunk/js/playS2cById.js');
const ENTITY_METADATA_ID = _playS2cById.indexOf('entity_metadata'); // 0x61
import {
  buildFrame,
  createFrameProcessor,
  writeVarInt,
  writeString,
} from '../client/wire.js';
import { offlineUUID } from '../client/protocol.js';
import {
  onCaptureReady,
  getCapture,
  isCaptureReady,
  getUpstreamClient,
  setDownstreamClient,
  getEntityTracker,
  getDimensionName,
  getLoginData,
  getCapturedPosition,
} from '../client/captureStore.js';
import { createServerLogger } from './logger.js';
import { logPingTickFromEnv } from '../client/logNoise.js';
import { createOnPacket } from './onPacket.js';

function getChunkFilesSync(dir) {
  let results = [];
  try {
    const list = fs.readdirSync(dir, { withFileTypes: true });
    for (const file of list) {
      const res = path.resolve(dir, file.name);
      if (file.isDirectory()) {
        results = results.concat(getChunkFilesSync(res));
      } else if (file.name.endsWith('.chunk')) {
        results.push(res);
      }
    }
  } catch (e) {
    // ignore
  }
  return results;
}

function packLpVec3(x, y, z) {
  const scale = 1;
  const maxQ = 32766;
  const clamp = (val) => Math.max(-1.0, Math.min(1.0, val));
  const vx = Math.round(((clamp(x) + 1.0) * maxQ) / 2.0) & 0x7fff;
  const vy = Math.round(((clamp(y) + 1.0) * maxQ) / 2.0) & 0x7fff;
  const vz = Math.round(((clamp(z) + 1.0) * maxQ) / 2.0) & 0x7fff;
  const packedBig = (BigInt(vz) << 33n) | (BigInt(vy) << 18n) | (BigInt(vx) << 3n) | BigInt(scale);
  const buf = Buffer.alloc(6);
  buf.writeUInt8(Number(packedBig & 0xffn), 0);
  buf.writeUInt8(Number((packedBig >> 8n) & 0xffn), 1);
  buf.writeUInt32BE(Number((packedBig >> 16n) & 0xffffffffn), 2);
  return buf;
}

function serializeSpawnEntityPacket(ent) {
  const idBuf = writeVarInt(ent.id);
  const uuidClean = (ent.uuid || '').replace(/-/g, '');
  const uuidBuf = uuidClean.length === 32 ? Buffer.from(uuidClean, 'hex') : Buffer.alloc(16);
  const typeBuf = writeVarInt(ent.type);
  
  const coordsBuf = Buffer.alloc(24);
  coordsBuf.writeDoubleBE(ent.x, 0);
  coordsBuf.writeDoubleBE(ent.y, 8);
  coordsBuf.writeDoubleBE(ent.z, 16);
  
  // Use the last tracked velocity (updated by entity_velocity packets)
  const vel = ent.vel || { x: 0, y: 0, z: 0 };
  const velBuf = packLpVec3(vel.x, vel.y, vel.z);

  const rotBuf = Buffer.alloc(3);
  rotBuf.writeInt8(ent.rot.pitch || 0, 0);   // pitch
  rotBuf.writeInt8(ent.rot.yaw   || 0, 1);   // yaw
  rotBuf.writeInt8(ent.rot.headYaw || ent.rot.yaw || 0, 2); // headYaw (not headPitch)
  
  const dataBuf = writeVarInt(ent.data || 0);
  return Buffer.concat([idBuf, uuidBuf, typeBuf, coordsBuf, velBuf, rotBuf, dataBuf]);
}

function serverLogOptions() {
  const debug =
    process.env.MC_SERVER_DEBUG === '1' || process.env.MC_CLIENT_DEBUG === '1';
  const logLevel =
    LOG_LEVELS[process.env.MC_SERVER_LOG_LEVEL] ??
    LOG_LEVELS[process.env.MC_CLIENT_LOG_LEVEL] ??
    (debug ? LOG_LEVELS.debug : LOG_LEVELS.info);
  return { debug, logLevel, logPingTick: logPingTickFromEnv(process.env, 'server') };
}

/**
 * @param {import('node:net').Socket} sock
 * @param {{ logLevel?: number, debug?: boolean, logPingTick?: boolean }} [opts]
 */
export function handleClient(sock, opts = {}) {
  const { debug, logLevel, logPingTick } = { ...serverLogOptions(), ...opts };

  let phase = 'handshake';
  let username = '';
  let configCursor = 0;
  let configAwait = null;
  let playJoinDone = false;
  let keepAliveTimer = null;
  let keepAliveId = 1000n;
  
  setDownstreamClient(sock, () => phase, (id, payload) => {
    send(sock, id, payload);
  });

  const logger = createServerLogger({
    getPhase: () => phase,
    logLevel,
    debug,
    logPingTick,
  });

  function startPlayTimers(sock) {
    if (keepAliveTimer) return;
    logger.debug('keep_alive timer', chalk.dim('every 15s during play'));
    keepAliveTimer = setInterval(() => {
      if (phase !== 'play') return;
      const payload = Buffer.alloc(8);
      payload.writeBigInt64BE(keepAliveId);
      send(sock, PLAY.KEEP_ALIVE, payload);
      keepAliveId += 1n;
    }, 15000);
  }

  function setPhase(next) {
    if (phase === next) return;
    const prev = phase;
    phase = next;
    logger.phaseChange(prev, next);
    if (next === 'play' && sock) {
      startPlayTimers(sock);
    }
  }

  function send(sock, id, payload) {
    const pl = payload ?? Buffer.alloc(0);
    sock.write(buildFrame(id, pl));
    logger.s2c(id, pl);
  }

  function sendLoginSuccess(sock, username) {
    const uuid = offlineUUID(username);
    const body = Buffer.concat([uuid, writeString(username), writeVarInt(0)]);
    send(sock, LOGIN.SUCCESS, body);
  }

  function replayConfigUntil(stopAfterId) {
    const snap = getCapture();
    let sent = 0;
    while (configCursor < snap.config.length) {
      const pkt = snap.config[configCursor++];
      send(sock, pkt.id, pkt.payload);
      sent++;
      if (pkt.id === stopAfterId) break;
    }
    return sent;
  }

  function replayPlayJoin(sock) {
    const snap = getCapture();
    let sent = 0;
    let mapChunksSkipped = 0;

    for (const pkt of snap.playJoin) {
      if (pkt.id === PLAY.MAP_CHUNK) {
        mapChunksSkipped++;
        continue;
      }
      send(sock, pkt.id, pkt.payload);
      sent++;
    }

    const dim = getDimensionName();
    let chunkDir = path.resolve('chunks');
    if (dim && fs.existsSync(path.join(chunkDir, dim))) {
      chunkDir = path.join(chunkDir, dim);
    }
    const chunkFiles = getChunkFilesSync(chunkDir);

    const pos = getCapturedPosition();
    const cx = pos ? Math.floor(pos.x / 16) : 0;
    const cz = pos ? Math.floor(pos.z / 16) : 0;

    const loginData = getLoginData();
    const viewDistance = loginData ? loginData.viewDistance : 3;
    const maxDistance = viewDistance + 1;

    const sortedChunks = [];
    for (const file of chunkFiles) {
      const filename = path.basename(file);
      const parts = filename.split('_');
      if (parts.length >= 2) {
        const chunkX = parseInt(parts[0], 10);
        const chunkZ = parseInt(parts[1], 10);
        if (!isNaN(chunkX) && !isNaN(chunkZ)) {
          const dist = Math.max(Math.abs(chunkX - cx), Math.abs(chunkZ - cz));
          if (dist <= maxDistance) {
            sortedChunks.push({ file, chunkX, chunkZ, dist });
          }
        }
      }
    }

    sortedChunks.sort((a, b) => a.dist - b.dist);
    logger.info('chunks_replay', chalk.dim(`Sorted ${sortedChunks.length} chunks concentrically around center (${cx},${cz}) with maxDistance ${maxDistance}`));

    let fsChunksSent = 0;
    for (const item of sortedChunks) {
      try {
        const payload = fs.readFileSync(item.file);
        send(sock, PLAY.MAP_CHUNK, payload);
        fsChunksSent++;
      } catch (err) {
        logger.error(`Failed to read chunk file ${item.file}: ${err.message}`);
      }
    }

    playJoinDone = true;
    const skipNote =
      mapChunksSkipped > 0 ? ` (${mapChunksSkipped} original chunks skipped, ${fsChunksSent} chunks loaded from disk)` : '';
    logger.event('play_join replay', chalk.dim(`${sent} S2C packets${skipNote}`));
  }

  function beginConfig(sock) {
    setPhase('config');
    configCursor = 0;
    configAwait = CFG.SELECT_KNOWN_PACKS;

    const run = () => {
      const n = replayConfigUntil(CFG.SELECT_KNOWN_PACKS);
      logger.event('config replay (1/2)', chalk.dim(`${n} S2C until select_known_packs`));
      configAwait = CFG.FINISH;
    };

    if (isCaptureReady()) run();
    else {
      logger.info('waiting for capture', chalk.dim('(upstream client still joining)'));
      onCaptureReady(run);
    }
  }

  function continueConfigAfterPacks(sock) {
    const n = replayConfigUntil(CFG.FINISH);
    logger.event('config replay (2/2)', chalk.dim(`${n} S2C until finish_configuration`));
    configAwait = 'c2s_finish';
  }

  function handleConfigC2s(sock, id, payload) {
    logger.c2s(id, payload);

    if (id === CFG.C2S_CUSTOM_PAYLOAD) {
      const upstream = getUpstreamClient();
      if (upstream.sock && upstream.getPhase() === 'play' && upstream.send) {
        upstream.send(PLAY.C2S_CUSTOM_PAYLOAD, payload); // custom_payload in play phase is 0x15
      }
      return true;
    }
    if (id === CFG.C2S_SELECT_KNOWN_PACKS && configAwait === CFG.FINISH) {
      continueConfigAfterPacks(sock);
      return true;
    }
    // C2S keep_alive is the client's reply to our S2C challenge — do not echo S2C back.
    if (id === CFG.C2S_KEEP_ALIVE) return true;
    if (id === CFG.C2S_PONG) return true;
    if (id === CFG.C2S_FINISH && configAwait === 'c2s_finish') {
      setPhase('play_join');
      configAwait = null;
      replayPlayJoin(sock);
      return true;
    }
    return false;
  }

  /** Packets that belong only to the proxy↔client link and must NOT be relayed upstream. */
  function isLocalOnlyC2s(id) {
    return (
      id === PLAY.C2S_KEEP_ALIVE ||
      id === PLAY.C2S_PONG ||
      id === PLAY.C2S_CHUNK_BATCH_RECEIVED ||
      id === PLAY.C2S_PLAYER_LOADED
    );
  }

  function handlePlayC2sCommon(sock, id, payload) {
    if (isLocalOnlyC2s(id)) return true;

    // Forward everything else to upstream when in play phase
    if (phase === 'play') {
      const upstream = getUpstreamClient();
      if (upstream.sock && upstream.getPhase() === 'play' && upstream.send) {
        upstream.send(id, payload);
      }
    }
    return true;
  }

  function handlePlayJoinC2s(sock, id, payload) {
    logger.c2s(id, payload);
    if (!playJoinDone) {
      logger.warn('C2S before play_join replay done', chalk.dim(`0x${id.toString(16)}`));
      return true;
    }
    if (id === PLAY.C2S_PLAYER_LOADED) {
      const tracker = getEntityTracker();
      if (tracker && tracker.entities && tracker.entities.size > 0) {
        logger.info('spawning tracked entities for downstream client', chalk.dim(`${tracker.entities.size} entities`));
        for (const ent of tracker.entities.values()) {
          const metadataId = ENTITY_METADATA_ID;
          const metaPkts = ent.statePackets ? ent.statePackets.filter(p => p.id === metadataId) : [];
          const otherPkts = ent.statePackets ? ent.statePackets.filter(p => p.id !== metadataId) : [];

          // Bundle: spawn_entity + entity_metadata only (mirrors real server pattern)
          send(sock, PLAY.BUNDLE_DELIMITER, BUNDLE_DELIMITER);
          send(sock, 0x01, serializeSpawnEntityPacket(ent));
          for (const pkt of metaPkts) {
            send(sock, pkt.id, pkt.payload);
          }
          send(sock, PLAY.BUNDLE_DELIMITER, BUNDLE_DELIMITER);

          // Remaining state packets (equipment, attributes, etc.) sent outside bundle
          for (const pkt of otherPkts) {
            send(sock, pkt.id, pkt.payload);
          }
        }
      }
      setPhase('play');
      logger.event('play ready', chalk.dim(username));
      return true;
    }
    return handlePlayC2sCommon(sock, id, payload);
  }

  function handlePlayC2s(sock, id, payload) {
    logger.c2s(id, payload);
    if (id === PLAY.C2S_PLAYER_LOADED) return true;
    return handlePlayC2sCommon(sock, id, payload);
  }

  const onPacket = createOnPacket({
    getPhase: () => phase,
    logger,
    send,
    setPhase,
    sendLoginSuccess,
    beginConfig,
    setUsername: (name) => { username = name; },
    handleConfigC2s,
    handlePlayJoinC2s,
    handlePlayC2s,
  });

  const feed = createFrameProcessor((id, payload) => {
    onPacket(sock, id, payload);
  });

  sock.on('data', (chunk) => {
    try {
      feed(chunk);
    } catch (e) {
      logger.error(e.message);
      sock.destroy();
    }
  });
  sock.on('error', (e) => logger.error('socket', chalk.red(e.message)));
  sock.on('close', () => {
    setDownstreamClient(null, () => 'handshake', null);
    if (keepAliveTimer) clearInterval(keepAliveTimer);
    logger.info('client disconnected', chalk.dim(username || '?'));
  });
}

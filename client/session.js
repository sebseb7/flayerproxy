import fsSync from 'node:fs';
import fs from 'node:fs/promises';
import path from 'node:path';
import chalk from 'chalk';
import { PROTOCOL, HS_LOGIN, LOGIN, CFG, PLAY, CLIENT_TICK_MS } from './constants.js';
import { createFrameProcessor, buildFrame, writeVarInt, writeString } from './wire.js';
import { offlineUUID } from './protocol.js';
import { createLogger } from './logger.js';
import { createOnPacket } from './onPacket.js';
import {
  recordConfigS2c,
  recordPlayJoinS2c,
  trackPlayPacket,
  trackC2sPacket,
  clearPlayJoinCapture,
  markCaptureReady,
  getCapture,
  setUpstreamClient,
  getDownstreamClient,
  setEntityTracker,
  getDimensionName,
} from './captureStore.js';

import { getChunkDataLen } from './chunk.js';

function loadSavedChunkCoordsSync() {
  const coords = new Set();
  const getFiles = (dir) => {
    try {
      const list = fsSync.readdirSync(dir, { withFileTypes: true });
      for (const file of list) {
        const res = path.resolve(dir, file.name);
        if (file.isDirectory()) {
          getFiles(res);
        } else if (file.name.endsWith('_map.chunk')) {
          const parts = file.name.split('_');
          if (parts.length >= 2) {
            try {
              const payload = fsSync.readFileSync(res);
              const dataLen = getChunkDataLen(payload);
              if (dataLen >= 500) {
                coords.add(`${parts[0]},${parts[1]}`);
              } else {
                fsSync.unlinkSync(res); // delete placeholder from disk
              }
            } catch (err) {}
          }
        }
      }
    } catch (e) {}
  };
  getFiles(path.resolve('chunks'));
  return coords;
}


export function createSession(config) {
  const { host, port, username, debug, logLevel, logPingTick, autoRespawn = false } = config;

  let phase = 'connect';
  let tickTimer = null;
  /** @type {import('node:net').Socket | null} */
  let conn = null;
  let joinBurstLogged = false;
  const chunkDirsReady = new Set();

  const state = {
    entityId: null,
    dimension: null,
    playerLoadedSent: false,
    chunksLoadStarted: false,
    mapChunksSeen: 0,
    chunkCoords: loadSavedChunkCoordsSync(),
    sessionChunkCoords: new Set(),
    viewDistance: 3,
    chunkCenterX: 0,
    chunkCenterZ: 0,
    daylightTicking: false,
    tickingRunsNormally: false,
    pendingTeleport: null,
    playerDead: false,
    healthKnown: false,
    respawnRequested: false,
  };

  const logger = createLogger({
    getPhase: () => phase,
    logLevel,
    debug,
    logPingTick,
  });

  setEntityTracker(logger.entityTracker);

  function setPhase(next) {
    if (phase === next) return;
    const prev = phase;
    phase = next;
    logger.phaseChange(prev, next);
    if (next === 'play' && conn) startPlayTimers(conn);
  }

  function send(sock, id, payload, detail) {
    sock.write(buildFrame(id, payload));
    if (phase === 'play_join' || phase === 'play') {
      trackC2sPacket(id, payload);
    }
    logger.c2s(id, payload, detail);
  }

  function sendSilent(sock, id, payload) {
    sock.write(buildFrame(id, payload));
  }

  function resetPlayJoinState() {
    state.playerLoadedSent = false;
    state.chunksLoadStarted = false;
    state.mapChunksSeen = 0;
    state.sessionChunkCoords.clear();
    state.daylightTicking = false;
    state.tickingRunsNormally = false;
    state.pendingTeleport = null;
    state.healthKnown = false;
    joinBurstLogged = false;
  }

  function requestRespawn(sock) {
    if (state.respawnRequested) return;
    state.respawnRequested = true;
    send(sock, PLAY.C2S_CLIENT_COMMAND, writeVarInt(0), chalk.dim('PERFORM_RESPAWN'));
  }

  function enterDeath(sock, reason) {
    if (phase !== 'play_join') return;
    setPhase('death');
    logger.event('dead on join', chalk.dim(reason));
    if (autoRespawn) {
      requestRespawn(sock);
    } else {
      logger.info(
        'death',
        chalk.dim('auto respawn disabled; use --auto-respawn or MC_CLIENT_AUTO_RESPAWN=1'),
      );
    }
  }

  function beginRespawnJoin(sock) {
    if (phase !== 'death') return;
    state.playerDead = false;
    state.respawnRequested = false;
    clearPlayJoinCapture();
    resetPlayJoinState();
    setPhase('play_join');
    logger.event('respawn', chalk.dim('→ play_join (reload chunks)'));
  }

  function enterPlay(sock, reason) {
    if (phase !== 'play_join') return;
    markCaptureReady();
    const snap = getCapture();
    logger.info(
      'capture stored for server',
      chalk.dim(`config=${snap.config.length} play_join=${snap.playJoin.length}`),
    );
    setPhase('play');
    logger.event('play ready', chalk.dim(reason));
  }

  function playJoinChunksReady() {
    return state.sessionChunkCoords.has(`${state.chunkCenterX},${state.chunkCenterZ}`);
  }

  function finishPlayJoin(sock, reason) {
    if (phase !== 'play_join') return;

    if (state.pendingTeleport) {
      const pos = state.pendingTeleport;
      const posDetail = chalk.dim(
        `tp=${pos.id} (${pos.x.toFixed(2)},${pos.y.toFixed(2)},${pos.z.toFixed(2)})`
      );

      // 1. send C2S_TELEPORT_CONFIRM
      send(sock, PLAY.C2S_TELEPORT_CONFIRM, writeVarInt(pos.id), posDetail);

      // 2. send C2S_POSITION_LOOK
      const plBuf = Buffer.alloc(8 + 8 + 8 + 4 + 4 + 1);
      plBuf.writeDoubleBE(pos.x, 0);
      plBuf.writeDoubleBE(pos.y, 8);
      plBuf.writeDoubleBE(pos.z, 16);
      plBuf.writeFloatBE(pos.yaw, 24);
      plBuf.writeFloatBE(pos.pitch, 28);
      plBuf.writeUInt8(1, 32); // onGround = true
      send(sock, PLAY.C2S_POSITION_LOOK, plBuf, chalk.dim(`x=${pos.x.toFixed(2)},y=${pos.y.toFixed(2)},z=${pos.z.toFixed(2)},yaw=${pos.yaw.toFixed(1)},pitch=${pos.pitch.toFixed(1)}`));

      state.pendingTeleport = null;
    }

    if (!state.playerLoadedSent) {
      send(sock, PLAY.C2S_PLAYER_LOADED, Buffer.alloc(0));
      state.playerLoadedSent = true;
      logger.event('player_loaded', chalk.dim(reason));
    }
    enterPlay(sock, reason);
  }

  function tryFinishPlayJoin(sock, reason) {
    if (phase !== 'play_join') return;
    if (state.playerDead) return;

    const waiting = [];
    if (!state.chunksLoadStarted) waiting.push('game_event:level_chunks_load_start');
    if (!playJoinChunksReady()) {
      waiting.push(`center_chunk(${state.chunkCenterX},${state.chunkCenterZ})`);
    }
    if (!state.daylightTicking) waiting.push('update_time:tickDayTime');
    if (!state.tickingRunsNormally) waiting.push('set_ticking_state:unfrozen');
    if (!state.healthKnown) waiting.push('update_health');
    if (waiting.length > 0) {
      logger.debug('play_join waiting', chalk.dim(`${reason} → ${waiting.join(', ')}`));
      return;
    }

    finishPlayJoin(sock, reason);
  }

  function notePlayJoinPacket() {
    if (phase !== 'play_join' || joinBurstLogged) return;
    joinBurstLogged = true;
    logger.event('join burst', chalk.dim('login + chunks'));
  }

  function writeMapChunk(file, payload) {
    const dim = getDimensionName() || state.dimension || 'unknown';
    const dir = path.dirname(file);
    // Insert dimension as the first section of the path (under the chunks root).
    // Current layout is <chunksRoot>/d1/d2/d3/d4/file; we want
    // <chunksRoot>/<dimension>/d1/d2/d3/d4/file.
    const chunksRoot = path.dirname(path.dirname(path.dirname(path.dirname(dir))));
    const newDir = path.join(chunksRoot, dim, path.relative(chunksRoot, dir));
    const newFile = path.join(newDir, path.basename(file));
    logger.debug('map_chunk_write', chalk.dim(`dim="${dim}" ${file} → ${newFile}`));
    void (async () => {
      if (!chunkDirsReady.has(newDir)) {
        await fs.mkdir(newDir, { recursive: true });
        chunkDirsReady.add(newDir);
      }
      await fs.writeFile(newFile, payload);
    })().catch((e) => logger.error('map_chunk write', chalk.red(e.message)));
  }

  const onPacket = createOnPacket({
    getPhase: () => phase,
    logger,
    send,
    sendSilent,
    setPhase,
    enterDeath,
    beginRespawnJoin,
    tryFinishPlayJoin,
    notePlayJoinPacket,
    writeMapChunk,
    state,
  });

  function startPlayTimers(sock) {
    if (tickTimer) return;
    logger.debug('tick_end timer', chalk.dim(`${1000 / CLIENT_TICK_MS}/s during play`));
    tickTimer = setInterval(() => {
      if (phase !== 'play') return;

      const downstream = getDownstreamClient();
      if (downstream.sock && downstream.getPhase() === 'play') {
        return;
      }

      send(sock, PLAY.C2S_TICK_END, Buffer.alloc(0));
    }, CLIENT_TICK_MS);
  }

  function handshake(sock) {
    const payload = Buffer.concat([
      writeVarInt(PROTOCOL),
      writeString(host),
      Buffer.from([(port >> 8) & 0xff, port & 0xff]),
      writeVarInt(HS_LOGIN),
    ]);
    send(sock, 0x00, payload, chalk.dim(`protocol=${PROTOCOL}`));
    setPhase('login');
    send(
      sock,
      LOGIN.C2S_START,
      Buffer.concat([writeString(username), offlineUUID(username)]),
      chalk.dim(username),
    );
    logger.info(
      'connecting',
      chalk.white(`${host}:${port}`) +
      chalk.dim(' as ') +
      chalk.bold.white(username) +
      chalk.dim(` (protocol ${PROTOCOL})`),
    );
  }

  function attach(sock) {
    conn = sock;
    setUpstreamClient(sock, () => phase, (id, payload, detail) => {
      send(sock, id, payload, detail);
    });
    const feedFrames = createFrameProcessor((id, payload) => {
      if (phase === 'config' && id !== CFG.PING) {
        recordConfigS2c(id, payload);
      } else if (phase === 'play_join' && id !== PLAY.PING) {
        recordPlayJoinS2c(id, payload);
      }
      if ((phase === 'play_join' || phase === 'play') && id !== PLAY.PING) {
        trackPlayPacket(id, payload);
      }
      onPacket(sock, id, payload);
    });
    sock.on('data', (chunk) => {
      try {
        feedFrames(chunk);
      } catch (e) {
        logger.error(e.message);
        sock.destroy();
      }
    });
    sock.on('error', (e) => logger.error('socket', chalk.red(e.message)));
    sock.on('close', () => {
      setUpstreamClient(null, () => 'connect', null);
      if (tickTimer) clearInterval(tickTimer);
      logger.warn('disconnected');
      process.exit(sock.destroyed ? 0 : 1);
    });
  }

  function onConnect(sock) {
    handshake(sock);
  }

  function stop() {
    if (tickTimer) clearInterval(tickTimer);
    tickTimer = null;
    logger.close();
  }

  return { logger, attach, onConnect, stop };
}

import fs from 'node:fs/promises';
import path from 'node:path';
import chalk from 'chalk';
import { PROTOCOL, HS_LOGIN, LOGIN, PLAY } from './constants.js';
import { createFrameProcessor, buildFrame, writeVarInt, writeString } from './wire.js';
import { offlineUUID, expectedChunkGridCount } from './protocol.js';
import { createLogger } from './logger.js';
import { createOnPacket } from './onPacket.js';
import {
  recordConfigS2c,
  recordPlayJoinS2c,
  markCaptureReady,
  getCapture,
} from './captureStore.js';

export function createSession(config) {
  const { host, port, username, debug, logLevel } = config;

  let phase = 'connect';
  let tickTimer = null;
  let joinBurstLogged = false;
  const chunkDirsReady = new Set();

  const state = {
    playerLoadedSent: false,
    chunksLoadStarted: false,
    mapChunksSeen: 0,
    chunkCoords: new Set(),
    viewDistance: 3,
    chunkCenterX: 0,
    chunkCenterZ: 0,
    daylightTicking: false,
    tickingRunsNormally: false,
  };

  const logger = createLogger({
    getPhase: () => phase,
    logLevel,
    debug,
  });

  function setPhase(next) {
    if (phase === next) return;
    const prev = phase;
    phase = next;
    logger.phaseChange(prev, next);
  }

  function send(sock, id, payload, detail) {
    sock.write(buildFrame(id, payload));
    logger.c2s(id, payload, detail);
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
    startPlayTimers(sock);
  }

  function chunksInViewGrid() {
    const r = Math.max(0, state.viewDistance - 1);
    let n = 0;
    for (const key of state.chunkCoords) {
      const [xs, zs] = key.split(',');
      const cx = Number(xs);
      const cz = Number(zs);
      if (Math.abs(cx - state.chunkCenterX) <= r && Math.abs(cz - state.chunkCenterZ) <= r) n++;
    }
    return n;
  }

  function playJoinChunksReady() {
    return chunksInViewGrid() >= expectedChunkGridCount(state.viewDistance);
  }

  /** Vanilla: player_loaded after LevelLoadTracker; capture seals after view-distance chunk grid. */
  function finishPlayJoin(sock, reason) {
    if (phase !== 'play_join') return;
    if (!state.playerLoadedSent) {
      send(sock, PLAY.C2S_PLAYER_LOADED, Buffer.alloc(0));
      state.playerLoadedSent = true;
      logger.event('player_loaded', chalk.dim(reason));
    }
    enterPlay(sock, reason);
  }

  function tryFinishPlayJoin(sock, reason) {
    if (phase !== 'play_join') return;
    if (!state.chunksLoadStarted || !playJoinChunksReady()) return;
    if (!state.daylightTicking) return;
    if (!state.tickingRunsNormally) return;
    finishPlayJoin(sock, reason);
  }

  function notePlayJoinPacket() {
    if (phase !== 'play_join' || joinBurstLogged) return;
    joinBurstLogged = true;
    logger.event('join burst', chalk.dim('login + chunks (vanilla: LevelLoadTracker)'));
  }

  function writeMapChunk(file, payload) {
    const dir = path.dirname(file);
    void (async () => {
      if (!chunkDirsReady.has(dir)) {
        await fs.mkdir(dir, { recursive: true });
        chunkDirsReady.add(dir);
      }
      await fs.writeFile(file, payload);
    })().catch((e) => logger.error('map_chunk write', chalk.red(e.message)));
  }

  const onPacket = createOnPacket({
    getPhase: () => phase,
    logger,
    send,
    setPhase,
    enterPlay,
    tryFinishPlayJoin,
    finishPlayJoin,
    notePlayJoinPacket,
    writeMapChunk,
    state,
  });

  function startPlayTimers(sock) {
    if (tickTimer) return;
    logger.debug('tick_end timer', chalk.dim('every 1s while in play'));
    tickTimer = setInterval(() => {
      if (phase !== 'play') return;
      send(sock, PLAY.C2S_TICK_END, Buffer.alloc(0));
    }, 1000);
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
    const feedFrames = createFrameProcessor((id, payload) => {
      if (phase === 'config') recordConfigS2c(id, payload);
      else if (phase === 'play_join') recordPlayJoinS2c(id, payload);
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
  }

  return { logger, attach, onConnect, stop };
}

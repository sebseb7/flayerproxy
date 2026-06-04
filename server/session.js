import fs from 'node:fs/promises';
import path from 'node:path';
import chalk from 'chalk';
import { HS_LOGIN, LOGIN, CFG, PLAY, LOG_LEVELS } from '../client/constants.js';
import {
  buildFrame,
  createFrameProcessor,
  writeVarInt,
  writeString,
  readVarInt,
  readString,
} from '../client/wire.js';
import { offlineUUID } from '../client/protocol.js';
import { onCaptureReady, getCapture, isCaptureReady, getUpstreamClient, setDownstreamClient } from '../client/captureStore.js';
import { createServerLogger } from './logger.js';
import { logPingTickFromEnv } from '../client/logNoise.js';
import { createOnPacket } from './onPacket.js';

async function getChunkFiles(dir) {
  let results = [];
  try {
    const list = await fs.readdir(dir, { withFileTypes: true });
    for (const file of list) {
      const res = path.resolve(dir, file.name);
      if (file.isDirectory()) {
        results = results.concat(await getChunkFiles(res));
      } else if (file.name.endsWith('.chunk')) {
        results.push(res);
      }
    }
  } catch (e) {
    // ignore
  }
  return results;
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

  async function replayPlayJoin(sock) {
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

    const chunkFiles = await getChunkFiles(path.resolve('chunks'));
    let fsChunksSent = 0;
    for (const file of chunkFiles) {
      try {
        const payload = await fs.readFile(file);
        send(sock, PLAY.MAP_CHUNK, payload);
        fsChunksSent++;
      } catch (err) {
        logger.error(`Failed to read chunk file ${file}: ${err.message}`);
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

    if (id === CFG.C2S_SETTINGS || id === CFG.C2S_CUSTOM_PAYLOAD) return true;
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

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
import { onCaptureReady, getCapture, isCaptureReady } from '../client/captureStore.js';
import { createServerLogger } from './logger.js';
import { logPingTickFromEnv } from '../client/logNoise.js';

const HS_STATUS = 1;

const STATUS_JSON =
  '{"version":{"name":"1.21.10","protocol":773},"players":{"max":20,"online":0,"sample":[]},"description":{"text":"flayerproxy capture replay"}}';

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

  const logger = createServerLogger({
    getPhase: () => phase,
    logLevel,
    debug,
    logPingTick,
  });

  function setPhase(next) {
    if (phase === next) return;
    const prev = phase;
    phase = next;
    logger.phaseChange(prev, next);
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
      send(sock, pkt.id, pkt.payload);
      sent++;
    }
    playJoinDone = true;
    const skipNote =
      mapChunksSkipped > 0 ? ` (${mapChunksSkipped} map_chunk skipped)` : '';
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

  function isPlayMovementOrPlugin(id) {
    return (
      id === PLAY.C2S_CUSTOM_PAYLOAD ||
      id === PLAY.C2S_POSITION ||
      id === PLAY.C2S_POSITION_LOOK ||
      id === PLAY.C2S_MOVE_ROT ||
      id === PLAY.C2S_MOVE_STATUS ||
      id === PLAY.C2S_TICK_END
    );
  }

  function handlePlayC2sCommon(sock, id, payload) {
    if (isPlayMovementOrPlugin(id)) return true;
    if (id === PLAY.C2S_TELEPORT_CONFIRM) return true;
    if (id === PLAY.C2S_CHUNK_BATCH_RECEIVED) return true;
    // C2S keep_alive is the client's reply to our S2C challenge — do not echo S2C back.
    if (id === PLAY.C2S_KEEP_ALIVE) return true;
    if (id === PLAY.C2S_PONG) return true;
    return false;
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

  const feed = createFrameProcessor((id, payload) => {
    if (phase === 'handshake') {
      let o = 0;
      const next = readVarInt(payload, o);
      if (!next) return;
      o = next.next;
      const hostLen = readVarInt(payload, o);
      if (!hostLen) return;
      o = hostLen.next + hostLen.value + 2;
      const intention = readVarInt(payload, o);
      if (!intention) return;
      if (intention.value === HS_STATUS) {
        setPhase('status');
        return;
      }
      if (intention.value === HS_LOGIN) {
        setPhase('login');
        return;
      }
      throw new Error(`unsupported handshake intention ${intention.value}`);
    }

    if (phase === 'status') {
      logger.c2s(id, payload);
      if (id === 0x00) {
        send(sock, 0x00, writeString(STATUS_JSON));
        return;
      }
      if (id === 0x01 && payload.length >= 8) {
        send(sock, 0x01, payload.subarray(0, 8));
        sock.end();
        return;
      }
      return;
    }

    if (phase === 'login') {
      logger.c2s(id, payload);
      if (id === LOGIN.C2S_START) {
        const name = readString(payload, 0);
        username = name?.value ?? 'Player';
        sendLoginSuccess(sock, username);
        logger.info('login success', chalk.dim(username));
        return;
      }
      if (id === LOGIN.C2S_ACK) {
        beginConfig(sock);
        return;
      }
      return;
    }

    if (phase === 'config') {
      const handled = handleConfigC2s(sock, id, payload);
      if (!handled) logger.warn('unhandled C2S', chalk.dim(`0x${id.toString(16)} len=${payload.length}`));
      return;
    }

    if (phase === 'play_join') {
      const handled = handlePlayJoinC2s(sock, id, payload);
      if (!handled) logger.warn('unhandled C2S', chalk.dim(`0x${id.toString(16)} len=${payload.length}`));
      return;
    }

    if (phase === 'play') {
      const handled = handlePlayC2s(sock, id, payload);
      if (!handled) logger.warn('unhandled C2S', chalk.dim(`0x${id.toString(16)} len=${payload.length}`));
    }
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
  sock.on('close', () => logger.info('client disconnected', chalk.dim(username || '?')));
}

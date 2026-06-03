import chalk from 'chalk';
import {
  PROTOCOL,
  HS_LOGIN,
  LOGIN,
  CFG,
  PLAY,
  GAME_EVENT_LEVEL_CHUNKS_LOAD_START,
} from './constants.js';
import {
  createFrameProcessor,
  buildFrame,
  writeVarInt,
  writeString,
  readString,
} from './wire.js';
import {
  offlineUUID,
  readI64BE,
  parsePosition,
  parseUpdateTime,
  parseGameEvent,
  parseSetTickingState,
} from './protocol.js';
import { createLogger } from './logger.js';

export function createSession(config) {
  const { host, port, username, debug, logLevel } = config;

  let phase = 'connect';
  let playerLoadedSent = false;
  let chunksLoadStarted = false;
  let mapChunksSeen = 0;
  let daylightTicking = false;
  let tickingRunsNormally = false;
  let tickTimer = null;
  let joinBurstLogged = false;

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
    setPhase('play');
    logger.event('play ready', chalk.dim(reason));
    startPlayTimers(sock);
  }

  /**
   * Vanilla (1.21.10): play join ends when LevelLoadTracker is ready → player_loaded.
   * We approximate: chunks load started + at least one chunk + daylight ticking enabled
   * (update_time.tickDayTime mirrors GameRules.RULE_DAYLIGHT, not a phase id).
   * tick_end is sent from Minecraft.tick() each frame while TickRateManager.runsNormally(),
   * not per map_chunk.
   */
  function tryFinishPlayJoin(sock, reason) {
    if (phase !== 'play_join' || !playerLoadedSent) return;
    if (!chunksLoadStarted || mapChunksSeen === 0) return;
    if (!daylightTicking) return;
    if (!tickingRunsNormally) return;
    enterPlay(sock, reason);
  }

  function notePlayJoinPacket() {
    if (phase !== 'play_join' || joinBurstLogged) return;
    joinBurstLogged = true;
    logger.event('join burst', chalk.dim('login + chunks (vanilla: LevelLoadTracker)'));
  }

  function startPlayTimers(sock) {
    if (tickTimer) return;
    logger.debug('tick_end timer', chalk.dim('every 1s while in play'));
    tickTimer = setInterval(() => {
      if (phase !== 'play') return;
      send(sock, PLAY.C2S_TICK_END, Buffer.alloc(0));
    }, 1000);
  }

  function onPacket(sock, id, payload) {
    logger.s2c(id, payload);

    if (phase === 'login') {
      if (id === LOGIN.DISCONNECT) {
        const msg = readString(payload, 0);
        throw new Error('login disconnect: ' + (msg ? msg.value : '?'));
      }
      if (id === LOGIN.SUCCESS) {
        send(sock, LOGIN.C2S_ACK, Buffer.alloc(0));
        setPhase('config');
        logger.event('login ok', chalk.dim('→ configuration'));
        return;
      }
      return;
    }

    if (phase === 'config') {
      if (id === CFG.DISCONNECT) {
        const msg = readString(payload, 0);
        throw new Error('config disconnect: ' + (msg ? msg.value : '?'));
      }
      if (id === CFG.SELECT_KNOWN_PACKS) {
        send(sock, CFG.C2S_SELECT_KNOWN_PACKS, payload, chalk.dim('echo'));
        logger.event('select_known_packs', chalk.dim('echoed to server'));
        return;
      }
      if (id === CFG.KEEP_ALIVE) {
        const k = readI64BE(payload, 0);
        if (k) {
          const out = Buffer.alloc(8);
          out.writeBigInt64BE(k.value);
          send(sock, CFG.C2S_KEEP_ALIVE, out, chalk.dim(`id=${k.value}`));
        }
        return;
      }
      if (id === CFG.PING) {
        const p = readVarInt(payload, 0);
        if (p) send(sock, CFG.C2S_PONG, writeVarInt(p.value), chalk.dim(`id=${p.value}`));
        return;
      }
      if (id === CFG.FINISH) {
        send(sock, CFG.C2S_FINISH, Buffer.alloc(0));
        logger.event('config finish', chalk.dim('→ play join'));
        setPhase('play_join');
        return;
      }
      return;
    }

    if (phase === 'play_join' || phase === 'play') {
      if (phase === 'play_join') {
        notePlayJoinPacket();
      }

      if (id === PLAY.GAME_EVENT) {
        const ev = parseGameEvent(payload);
        if (ev?.event === GAME_EVENT_LEVEL_CHUNKS_LOAD_START) {
          chunksLoadStarted = true;
          logger.debug('LEVEL_CHUNKS_LOAD_START', chalk.dim('LevelLoadTracker'));
        }
        return;
      }

      if (id === PLAY.SET_TICKING_STATE) {
        const ts = parseSetTickingState(payload);
        if (ts) {
          tickingRunsNormally = !ts.isFrozen;
          tryFinishPlayJoin(sock, 'set_ticking_state unfrozen');
        }
        return;
      }

      if (id === PLAY.UPDATE_TIME) {
        const t = parseUpdateTime(payload);
        if (t?.tickDayTime) {
          daylightTicking = true;
          tryFinishPlayJoin(sock, 'update_time tickDayTime=true');
        }
        return;
      }

      if (id === PLAY.MAP_CHUNK) {
        mapChunksSeen++;
        tryFinishPlayJoin(sock, 'map_chunk received');
        return;
      }

      if (id === PLAY.POSITION) {
        const pos = parsePosition(payload);
        if (pos) {
          const posDetail = chalk.dim(
            `tp=${pos.teleportId} (${pos.x.toFixed(2)},${pos.y.toFixed(2)},${pos.z.toFixed(2)})`,
          );
          send(sock, PLAY.C2S_TELEPORT_CONFIRM, writeVarInt(pos.teleportId), posDetail);
          if (!playerLoadedSent) {
            send(sock, PLAY.C2S_PLAYER_LOADED, Buffer.alloc(0));
            playerLoadedSent = true;
            logger.event('player_loaded', posDetail);
            tryFinishPlayJoin(sock, 'player_loaded');
          }
        }
        return;
      }
      if (id === PLAY.KEEP_ALIVE) {
        const k = readI64BE(payload, 0);
        if (k) {
          const out = Buffer.alloc(8);
          out.writeBigInt64BE(k.value);
          send(sock, PLAY.C2S_KEEP_ALIVE, out, chalk.dim(`id=${k.value}`));
        }
        return;
      }
      if (id === PLAY.PING) {
        if (payload.length >= 4) {
          send(sock, PLAY.C2S_PONG, payload.subarray(0, 4));
        }
        return;
      }
      if (id === PLAY.CHUNK_BATCH_FINISHED) {
        const batch = Buffer.alloc(4);
        batch.writeFloatBE(6.0);
        send(sock, PLAY.C2S_CHUNK_BATCH_RECEIVED, batch, chalk.dim('perSec=6'));
        enterPlay(sock, 'chunk_batch_finished');
        return;
      }
    }
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
    const feedFrames = createFrameProcessor((id, payload) => onPacket(sock, id, payload));
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

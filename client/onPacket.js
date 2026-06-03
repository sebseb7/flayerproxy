import chalk from 'chalk';
import { LOGIN, CFG, PLAY, GAME_EVENT_LEVEL_CHUNKS_LOAD_START } from './constants.js';
import { writeVarInt, readVarInt, readString } from './wire.js';
import {
  readI64BE,
  parsePosition,
  parseUpdateTime,
  parseGameEvent,
  parseSetTickingState,
  parseLoginViewDistance,
  parseUpdateViewPosition,
} from './protocol.js';
import { getLocationFromChunkPayload, mapChunkWirePath } from './chunk.js';

/** @param {object} ctx session callbacks + mutable `state` for play-join flags */
export function createOnPacket(ctx) {
  const {
    getPhase,
    logger,
    send,
    sendSilent,
    setPhase,
    tryFinishPlayJoin,
    finishPlayJoin,
    notePlayJoinPacket,
    writeMapChunk,
    state,
  } = ctx;

  return function onPacket(sock, id, payload) {
    const phase = getPhase();

    if (phase === 'config' && id === CFG.PING) {
      const p = readVarInt(payload, 0);
      if (p) sendSilent(sock, CFG.C2S_PONG, writeVarInt(p.value));
      return;
    }
    if ((phase === 'play_join' || phase === 'play') && id === PLAY.PING) {
      if (payload.length >= 4) {
        sendSilent(sock, PLAY.C2S_PONG, payload.subarray(0, 4));
      }
      return;
    }

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
          state.chunksLoadStarted = true;
        }
        return;
      }

      if (id === PLAY.SET_TICKING_STATE) {
        const ts = parseSetTickingState(payload);
        if (ts) {
          state.tickingRunsNormally = !ts.isFrozen;
          tryFinishPlayJoin(sock, 'set_ticking_state unfrozen');
        }
        return;
      }

      if (id === PLAY.UPDATE_TIME) {
        const t = parseUpdateTime(payload);
        if (t?.tickDayTime) {
          state.daylightTicking = true;
          tryFinishPlayJoin(sock, 'update_time tickDayTime=true');
        }
        return;
      }

      if (id === PLAY.LOGIN) {
        const vd = parseLoginViewDistance(payload);
        if (vd != null) state.viewDistance = vd;
        return;
      }

      if (id === PLAY.UPDATE_VIEW_POSITION) {
        const vp = parseUpdateViewPosition(payload);
        if (vp) {
          state.chunkCenterX = vp.chunkX;
          state.chunkCenterZ = vp.chunkZ;
        }
        return;
      }

      if (id === PLAY.MAP_CHUNK) {
        state.mapChunksSeen++;
        const loc = getLocationFromChunkPayload(payload);
        if (loc) state.chunkCoords.add(`${loc.chunkX},${loc.chunkZ}`);
        tryFinishPlayJoin(
          sock,
          loc ? `map_chunk @ ${loc.chunkX},${loc.chunkZ}` : 'map_chunk received',
        );
        if (loc) {
          const file = mapChunkWirePath(loc);
          writeMapChunk(file, payload);
          logger.debug(
            'map_chunk',
            chalk.dim(
              `chunk(${loc.chunkX},${loc.chunkZ}) block(${loc.blockX},${loc.blockZ}) → ${file}`,
            ),
          );
        }
        return;
      }

      if (id === PLAY.POSITION) {
        const pos = parsePosition(payload);
        if (pos) {
          const posDetail = chalk.dim(
            `tp=${pos.teleportId} (${pos.x.toFixed(2)},${pos.y.toFixed(2)},${pos.z.toFixed(2)})`,
          );
          send(sock, PLAY.C2S_TELEPORT_CONFIRM, writeVarInt(pos.teleportId), posDetail);
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
      if (id === PLAY.CHUNK_BATCH_FINISHED) {
        const batch = Buffer.alloc(4);
        batch.writeFloatBE(6.0);
        send(sock, PLAY.C2S_CHUNK_BATCH_RECEIVED, batch, chalk.dim('perSec=6'));
        tryFinishPlayJoin(sock, 'chunk_batch_finished');
        return;
      }
    }
  };
}

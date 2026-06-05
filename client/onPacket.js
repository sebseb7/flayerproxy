import chalk from 'chalk';
import crypto from 'node:crypto';
import https from 'node:https';
import path from 'node:path';
import { LOGIN, CFG, PLAY, GAME_EVENT_LEVEL_CHUNKS_LOAD_START } from './constants.js';
import { writeVarInt, readVarInt, readString, buildFrame } from './wire.js';
import { getDownstreamClient, recordPlayJoinS2c, trackPlayPacket } from './captureStore.js';
import {
  readI64BE,
} from './protocol.js';
import { getLocationFromChunkPayload, mapChunkWirePath, getChunkDataLen } from './chunk.js';
import { formatEntityType } from './entityTypeNames.js';
import { createRequire } from 'node:module';
const require = createRequire(import.meta.url);
const playS2cById = require('../libchunk/js/playS2cById.js');
const {
  parsePlayLogin,
  parsePosition,
  parseUpdateTime,
  parseGameEvent,
  parseSetTickingState,
  parseUpdateHealth,
  parseUpdateViewPosition,
  parseRespawn,
  parseSpawnEntity,
  parseEntityMetadata,
  parseEntityUpdateAttributes,
} = require('../libchunk/js/index.js');




function parseEncryptionBegin(payload) {
  let off = 0;
  const serverIdResult = readString(payload, off);
  if (!serverIdResult) throw new Error('failed to read serverId');
  const serverId = serverIdResult.value;
  off = serverIdResult.next;

  const pkLenResult = readVarInt(payload, off);
  if (!pkLenResult) throw new Error('failed to read publicKey len');
  off = pkLenResult.next;
  const publicKey = payload.subarray(off, off + pkLenResult.value);
  off += pkLenResult.value;

  const vtLenResult = readVarInt(payload, off);
  if (!vtLenResult) throw new Error('failed to read verifyToken len');
  off = vtLenResult.next;
  const verifyToken = payload.subarray(off, off + vtLenResult.value);
  off += vtLenResult.value;

  let shouldAuthenticate = true;
  if (off < payload.length) {
    shouldAuthenticate = payload[off] !== 0;
  }

  return { serverId, publicKey, verifyToken, shouldAuthenticate };
}

function minecraftHash(serverId, sharedSecret, publicKey) {
  const sha = crypto.createHash('sha1');
  sha.update(serverId);
  sha.update(sharedSecret);
  sha.update(publicKey);
  const hash = sha.digest();

  const isNegative = (hash[0] & 0x80) !== 0;
  if (isNegative) {
    const inverted = Buffer.alloc(hash.length);
    let carry = 1;
    for (let i = hash.length - 1; i >= 0; i--) {
      const val = (~hash[i] & 0xff) + carry;
      inverted[i] = val & 0xff;
      carry = val >> 8;
    }
    let hex = inverted.toString('hex').replace(/^0+/, '');
    return '-' + hex;
  } else {
    return hash.toString('hex').replace(/^0+/, '');
  }
}

function mojangSessionJoin(accessToken, profileId, serverIdHash) {
  return new Promise((resolve, reject) => {
    const body = JSON.stringify({
      accessToken,
      selectedProfile: profileId,
      serverId: serverIdHash,
    });

    const req = https.request({
      hostname: 'sessionserver.mojang.com',
      port: 443,
      path: '/session/minecraft/join',
      method: 'POST',
      headers: {
        'Content-Type': 'application/json',
        'Content-Length': Buffer.byteLength(body),
        'User-Agent': 'flayerproxy-mc_client',
      },
    }, (res) => {
      let data = '';
      res.on('data', (chunk) => { data += chunk; });
      res.on('end', () => {
        if (res.statusCode === 204) {
          resolve();
        } else {
          reject(new Error(`Mojang join failed (status ${res.statusCode}): ${data}`));
        }
      });
    });

    req.on('error', (err) => {
      reject(err);
    });

    req.write(body);
    req.end();
  });
}

/** @param {object} ctx session callbacks + mutable `state` for play-join flags */
export function createOnPacket(ctx) {
  const {
    getPhase,
    logger,
    send,
    sendSilent,
    setPhase,
    enterDeath,
    beginRespawnJoin,
    tryFinishPlayJoin,
    notePlayJoinPacket,
    writeMapChunk,
    postBinaryPayload,
    state,
    accessToken,
    profileId,
    enableEncryption,
    setCompressThreshold,
  } = ctx;

  let inBundle = false;
  let bundlePackets = [];

  function getEntityIdOfMetadata(payload) {
    try {
      const meta = parseEntityMetadata(payload);
      return meta ? meta.entityId : null;
    } catch (e) {
      return null;
    }
  }

  function getEntityIdOfAttributes(payload) {
    try {
      const attrs = parseEntityUpdateAttributes(payload);
      return attrs ? attrs.entityId : null;
    } catch (e) {
      return null;
    }
  }

  function postSingleSpawnEntity(id, payload) {
    try {
      const spawn = parseSpawnEntity(payload);
      if (!spawn) return;
      const { x, y, z } = spawn;
      const blockX = Math.floor(x);
      const blockY = Math.floor(y);
      const blockZ = Math.floor(z);
      const chunkX = Math.floor(blockX / 16);
      const chunkZ = Math.floor(blockZ / 16);
      const rx = Math.floor(chunkX / 512);
      const rz = Math.floor(chunkZ / 512);
      const cx = Math.floor(chunkX / 32);
      const cz = Math.floor(chunkZ / 32);

      const dim = state.dimension || 'unknown';
      const relativePath = path.join(dim, 'spawn_entity', `rx${rx}`, `rz${rz}`, `cx${cx}`, `cz${cz}`, `x${blockX}_y${blockY}_z${blockZ}.spawn_entity.wire`);
      const fileName = `x${blockX}_y${blockY}_z${blockZ}.spawn_entity.wire`;

      const packetFrame = buildFrame(id, payload);

      if (postBinaryPayload) {
        postBinaryPayload({
          relativePath,
          fileName,
          payload: packetFrame,
          headers: {
            'X-Dimension': dim,
            'X-Packet-Name': 'spawn_entity',
            'X-Packet-Decode-Name': 'spawn_entity',
          },
          successLabel: 'spawn_entity_post',
          errorLabel: 'spawn_entity post',
        });
      }
    } catch (e) {
      logger.error('failed to post spawn_entity', e.message);
    }
  }

  function postSpawnWithMetadata(ent, metadataId, metadataPayload) {
    try {
      const { x, y, z } = ent;
      const blockX = Math.floor(x);
      const blockY = Math.floor(y);
      const blockZ = Math.floor(z);
      const chunkX = Math.floor(blockX / 16);
      const chunkZ = Math.floor(blockZ / 16);
      const rx = Math.floor(chunkX / 512);
      const rz = Math.floor(chunkZ / 512);
      const cx = Math.floor(chunkX / 32);
      const cz = Math.floor(chunkZ / 32);

      const dim = state.dimension || 'unknown';
      const relativePath = path.join(dim, 'spawn_entity', `rx${rx}`, `rz${rz}`, `cx${cx}`, `cz${cz}`, `x${blockX}_y${blockY}_z${blockZ}.spawn_entity.wire`);
      const fileName = `x${blockX}_y${blockY}_z${blockZ}.spawn_entity.wire`;

      const spawnEntityId = playS2cById.indexOf('spawn_entity');
      if (spawnEntityId === -1) return;

      const spawnFrame = buildFrame(spawnEntityId, ent.spawnPayload);
      const metadataFrame = buildFrame(metadataId, metadataPayload);
      const bundlePayload = Buffer.concat([spawnFrame, metadataFrame]);

      if (postBinaryPayload) {
        postBinaryPayload({
          relativePath,
          fileName,
          payload: bundlePayload,
          headers: {
            'X-Dimension': dim,
            'X-Packet-Name': 'spawn_entity',
            'X-Packet-Decode-Name': 'spawn_entity',
          },
          successLabel: 'spawn_entity_post_metadata_update',
          errorLabel: 'spawn_entity post metadata update',
        });
      }
      ent.metadataPosted = true;
    } catch (e) {
      logger.error('failed to post spawn_entity metadata update', e.message);
    }
  }

  function handleBundle(packets) {
    // Find spawn_entity packets
    const spawns = packets.filter(pkt => playS2cById[pkt.id] === 'spawn_entity');
    for (const spawnPkt of spawns) {
      try {
        const spawn = parseSpawnEntity(spawnPkt.payload);
        if (!spawn) continue;
        const { entityId, x, y, z } = spawn;

        // Find matching metadata and attributes
        const metadataPkt = packets.find(pkt => playS2cById[pkt.id] === 'entity_metadata' && getEntityIdOfMetadata(pkt.payload) === entityId);
        const attributesPkt = packets.find(pkt => playS2cById[pkt.id] === 'entity_update_attributes' && getEntityIdOfAttributes(pkt.payload) === entityId);

        const matched = [spawnPkt];
        if (metadataPkt) {
          matched.push(metadataPkt);
          const ent = logger.entityTracker.get(entityId);
          if (ent) {
            ent.metadataPosted = true;
          }
        }
        if (attributesPkt) matched.push(attributesPkt);

        const blockX = Math.floor(x);
        const blockY = Math.floor(y);
        const blockZ = Math.floor(z);
        const chunkX = Math.floor(blockX / 16);
        const chunkZ = Math.floor(blockZ / 16);
        const rx = Math.floor(chunkX / 512);
        const rz = Math.floor(chunkZ / 512);
        const cx = Math.floor(chunkX / 32);
        const cz = Math.floor(chunkZ / 32);

        const dim = state.dimension || 'unknown';
        const relativePath = path.join(dim, 'spawn_entity', `rx${rx}`, `rz${rz}`, `cx${cx}`, `cz${cz}`, `x${blockX}_y${blockY}_z${blockZ}.spawn_entity.wire`);
        const fileName = `x${blockX}_y${blockY}_z${blockZ}.spawn_entity.wire`;

        const bundlePayload = Buffer.concat(matched.map(pkt => buildFrame(pkt.id, pkt.payload)));

        if (postBinaryPayload) {
          postBinaryPayload({
            relativePath,
            fileName,
            payload: bundlePayload,
            headers: {
              'X-Dimension': dim,
              'X-Packet-Name': 'spawn_entity',
              'X-Packet-Decode-Name': 'spawn_entity',
            },
            successLabel: 'spawn_entity_post',
            errorLabel: 'spawn_entity post',
          });
        }
      } catch (e) {
        logger.error('failed to process spawn bundle', e.message);
      }
    }
  }

  return function onPacket(sock, id, payload) {
    const phase = getPhase();

    if (phase === 'config' && id === CFG.PING) {
      const p = readVarInt(payload, 0);
      if (p) sendSilent(sock, CFG.C2S_PONG, writeVarInt(p.value));
      return;
    }
    if ((phase === 'play_join' || phase === 'death' || phase === 'play') && id === PLAY.PING) {
      const downstream = getDownstreamClient();
      if (downstream.sock && downstream.getPhase() === 'play' && downstream.send) {
        downstream.send(PLAY.PING, payload);
      } else {
        if (payload.length >= 4) {
          sendSilent(sock, PLAY.C2S_PONG, payload.subarray(0, 4));
        }
      }
      return;
    }

    logger.s2c(id, payload);

    if (phase === 'login') {
      if (id === LOGIN.DISCONNECT) {
        const msg = readString(payload, 0);
        throw new Error('login disconnect: ' + (msg ? msg.value : '?'));
      }
      if (id === LOGIN.COMPRESS) {
        const threshResult = readVarInt(payload, 0);
        if (threshResult) {
          const threshold = threshResult.value;
          logger.info('compression', chalk.green(`compression enabled (threshold=${threshold})`));
          setCompressThreshold(threshold);
        }
        return;
      }
      if (id === LOGIN.ENCRYPTION_BEGIN) {
        void (async () => {
          try {
            const { serverId, publicKey, verifyToken, shouldAuthenticate } = parseEncryptionBegin(payload);

            const sharedSecret = crypto.randomBytes(16);

            if (shouldAuthenticate) {
              if (!accessToken || !profileId) {
                throw new Error('online-mode server requires auth; set MC_ACCESS_TOKEN + MC_PROFILE_ID or run from repo root for MSA login');
              }
              const serverHash = minecraftHash(serverId, sharedSecret, publicKey);
              logger.info('auth', chalk.dim('Mojang session join...'));
              await mojangSessionJoin(accessToken, profileId, serverHash);
            }

            const encryptedSharedSecret = crypto.publicEncrypt({
              key: publicKey,
              format: 'der',
              type: 'spki',
              padding: crypto.constants.RSA_PKCS1_PADDING,
            }, sharedSecret);

            const encryptedVerifyToken = crypto.publicEncrypt({
              key: publicKey,
              format: 'der',
              type: 'spki',
              padding: crypto.constants.RSA_PKCS1_PADDING,
            }, verifyToken);

            const responseBody = Buffer.concat([
              writeVarInt(encryptedSharedSecret.length),
              encryptedSharedSecret,
              writeVarInt(encryptedVerifyToken.length),
              encryptedVerifyToken,
            ]);

            send(sock, LOGIN.C2S_ENCRYPTION_BEGIN, responseBody, chalk.dim('encryption_begin response'));

            enableEncryption(sharedSecret);
          } catch (err) {
            logger.error('encryption_begin failed', chalk.red(err.message));
            sock.destroy();
          }
        })();
        return;
      }
      if (id === LOGIN.SUCCESS) {
        send(sock, LOGIN.C2S_ACK, Buffer.alloc(0));
        setPhase('config');
        logger.event('login ok', chalk.dim('→ configuration'));
        const clientInfo = Buffer.from([5, 0x65, 0x6e, 0x5f, 0x55, 0x53, 10, 0, 1, 127, 1, 0, 1, 0]);
        send(sock, CFG.C2S_SETTINGS, clientInfo, chalk.dim('client_information'));
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
    if (phase === 'play_join' || phase === 'death' || phase === 'play') {
      if (phase === 'play_join') {
        notePlayJoinPacket();
      }

      // --- Bundle delimiters & packet accumulation ---
      if (id === 0x00) {
        if (!inBundle) {
          inBundle = true;
          bundlePackets = [];
        } else {
          inBundle = false;
          const packets = [...bundlePackets];
          bundlePackets = [];
          handleBundle(packets);
        }
      } else if (inBundle) {
        bundlePackets.push({ id, payload: Buffer.from(payload) });
      } else if (playS2cById[id] === 'spawn_entity') {
        postSingleSpawnEntity(id, payload);
      } else if (playS2cById[id] === 'entity_metadata') {
        const entityId = getEntityIdOfMetadata(payload);
        if (entityId !== null) {
          const ent = logger.entityTracker.get(entityId);
          if (ent && ent.spawnPayload && !ent.metadataPosted) {
            const typeName = formatEntityType(ent.type);
            if (typeName === 'item' || typeName === 'item_frame' || typeName === 'glow_item_frame') {
              postSpawnWithMetadata(ent, id, payload);
            }
          }
        }
      }

      if (id === PLAY.RESPAWN) {
        try {
          const respawn = parseRespawn(payload);
          if (respawn && respawn.dimensionName) {
            state.dimension = respawn.dimensionName;
            logger.debug('respawn', chalk.dim(`dimension changed to "${state.dimension}"`));
          }
        } catch (e) {
          // ignore parsing error
        }
        if (phase === 'death') {
          beginRespawnJoin(sock);
          recordPlayJoinS2c(id, payload);
          return;
        }
      }

      // --- Packets handled only by the proxy (never forwarded) ---

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
        if (phase !== 'death') {
          tryFinishPlayJoin(sock, 'chunk_batch_finished');
        }
        return;
      }

      // --- Packets that update local proxy state (AND get forwarded below) ---

      if (id === PLAY.GAME_EVENT) {
        const ev = parseGameEvent(payload);
        if (ev?.event === GAME_EVENT_LEVEL_CHUNKS_LOAD_START) {
          state.chunksLoadStarted = true;
        }
      } else if (id === PLAY.SET_TICKING_STATE) {
        const ts = parseSetTickingState(payload);
        if (ts) {
          state.tickingRunsNormally = !ts.isFrozen;
          if (phase !== 'death') {
            tryFinishPlayJoin(sock, 'set_ticking_state unfrozen');
          }
        }
      } else if (id === PLAY.UPDATE_TIME) {
        const t = parseUpdateTime(payload);
        if (t?.tickDayTime) {
          state.daylightTicking = true;
          if (phase !== 'death') {
            tryFinishPlayJoin(sock, 'update_time tickDayTime=true');
          }
        }
      } else if (id === PLAY.LOGIN) {
        const login = parsePlayLogin(payload);
        if (login) {
          state.entityId = login.entityId;
          state.viewDistance = login.viewDistance;
          state.dimension = login.dimensionName || null;
          logger.debug('login', chalk.dim(`dimension set to "${state.dimension}", login keys=${Object.keys(login).join(',')}`));
          if (login.hasDeath) {
            logger.debug(
              'login',
              chalk.dim('world_state.hasDeath=true (saved death location only)'),
            );
          }
        } else {
          logger.debug('login', chalk.dim('parsePlayLogin returned null/undefined'));
        }
        trackPlayPacket(id, payload);
      } else if (id === PLAY.UPDATE_HEALTH) {
        const h = parseUpdateHealth(payload);
        if (h) {
          const wasDead = state.playerDead;
          state.playerDead = h.health <= 0;
          if (phase === 'play_join') {
            state.healthKnown = true;
            if (state.playerDead && !wasDead) {
              enterDeath(sock, `update_health health=${h.health}`);
              return;
            }
            if (!state.playerDead) {
              tryFinishPlayJoin(sock, `update_health health=${h.health}`);
            }
          } else if (phase === 'death' && h.health > 0) {
            state.playerDead = false;
          }
        }
      } else if (id === PLAY.UPDATE_VIEW_POSITION) {
        const vp = parseUpdateViewPosition(payload);
        if (vp) {
          state.chunkCenterX = vp.chunkX;
          state.chunkCenterZ = vp.chunkZ;
          logger.debug(
            'update_view_position',
            chalk.dim(`chunk(${vp.chunkX},${vp.chunkZ})`),
          );
        }
      } else if (id === PLAY.MAP_CHUNK) {
        state.mapChunksSeen++;
        const loc = getLocationFromChunkPayload(payload);
        if (loc && phase === 'play_join' && state.playerDead) {
          enterDeath(sock, `playerDead map_chunk @ ${loc.chunkX},${loc.chunkZ}`);
          return;
        }
        if (loc) {
          const chunkDataLen = getChunkDataLen(payload);
          const isPlaceholder = chunkDataLen < 500;

          if (!isPlaceholder) {
            state.chunkCoords.add(`${loc.chunkX},${loc.chunkZ}`);
            state.sessionChunkCoords.add(`${loc.chunkX},${loc.chunkZ}`);
            const file = mapChunkWirePath(loc);
            writeMapChunk(file, payload);
          } else {
            if (state.chunkCoords.has(`${loc.chunkX},${loc.chunkZ}`)) {
              state.sessionChunkCoords.add(`${loc.chunkX},${loc.chunkZ}`);
              logger.info(
                'map_chunk_placeholder',
                chalk.dim(
                  `chunk(${loc.chunkX},${loc.chunkZ}) using existing disk cache (placeholder ignored)`,
                ),
              );
            } else {
              logger.warn(
                'map_chunk_placeholder',
                chalk.yellow(
                  `chunk(${loc.chunkX},${loc.chunkZ}) NOT cached (placeholder ignored)`,
                ),
              );
            }
          }
        }
        if (phase !== 'death') {
          tryFinishPlayJoin(
            sock,
            loc ? `map_chunk @ ${loc.chunkX},${loc.chunkZ}` : 'map_chunk received',
          );
        }
      } else if (id === PLAY.POSITION) {
        const pos = parsePosition(payload);
        if (pos) {
          if (phase === 'play') {
            state.chunkCenterX = Math.floor(pos.x / 16);
            state.chunkCenterZ = Math.floor(pos.z / 16);

            const downstream = getDownstreamClient();
            if (downstream.sock && downstream.getPhase() === 'play' && downstream.send) {
              downstream.send(PLAY.POSITION, payload);
            } else {
              const posDetail = chalk.dim(
                `tp=${pos.teleportId} (${pos.x.toFixed(2)},${pos.y.toFixed(2)},${pos.z.toFixed(2)})`
              );
              // 1. send C2S_TELEPORT_CONFIRM
              send(sock, PLAY.C2S_TELEPORT_CONFIRM, writeVarInt(pos.teleportId), posDetail);

              // 2. send C2S_POSITION_LOOK
              const plBuf = Buffer.alloc(8 + 8 + 8 + 4 + 4 + 1);
              plBuf.writeDoubleBE(pos.x, 0);
              plBuf.writeDoubleBE(pos.y, 8);
              plBuf.writeDoubleBE(pos.z, 16);
              plBuf.writeFloatBE(pos.yaw, 24);
              plBuf.writeFloatBE(pos.pitch, 28);
              plBuf.writeUInt8(1, 32); // onGround = true
              send(sock, PLAY.C2S_POSITION_LOOK, plBuf, chalk.dim(`x=${pos.x.toFixed(2)},y=${pos.y.toFixed(2)},z=${pos.z.toFixed(2)},yaw=${pos.yaw.toFixed(1)},pitch=${pos.pitch.toFixed(1)}`));
            }
          } else if (phase !== 'death') {
            const posDetail = chalk.dim(
              `tp=${pos.teleportId} (${pos.x.toFixed(2)},${pos.y.toFixed(2)},${pos.z.toFixed(2)})`
            );
            send(sock, PLAY.C2S_TELEPORT_CONFIRM, writeVarInt(pos.teleportId), posDetail);

            const plBuf = Buffer.alloc(8 + 8 + 8 + 4 + 4 + 1);
            plBuf.writeDoubleBE(pos.x, 0);
            plBuf.writeDoubleBE(pos.y, 8);
            plBuf.writeDoubleBE(pos.z, 16);
            plBuf.writeFloatBE(pos.yaw, 24);
            plBuf.writeFloatBE(pos.pitch, 28);
            plBuf.writeUInt8(1, 32); // onGround = true
            send(sock, PLAY.C2S_POSITION_LOOK, plBuf, chalk.dim(`x=${pos.x.toFixed(2)},y=${pos.y.toFixed(2)},z=${pos.z.toFixed(2)},yaw=${pos.yaw.toFixed(1)},pitch=${pos.pitch.toFixed(1)}`));

            state.chunkCenterX = Math.floor(pos.x / 16);
            state.chunkCenterZ = Math.floor(pos.z / 16);
            tryFinishPlayJoin(sock, 'position received');
          }
        }
        return; // POSITION has its own forwarding logic above
      }

      // --- Forward all S2C play packets to the downstream client ---
      if (phase === 'play') {
        const downstream = getDownstreamClient();
        if (
          downstream.sock &&
          (downstream.getPhase() === 'play' || downstream.getPhase() === 'play_join') &&
          downstream.send
        ) {
          downstream.send(id, payload);
        }
      }
    }
  };
}

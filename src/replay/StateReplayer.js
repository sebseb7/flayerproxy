const { createLogger } = require('../utils/logger');
const { buildClientboundPositionPacket } = require('../utils/positionSync');
const { LEVEL_CHUNKS_LOAD_START } = require('../utils/handoffSync');
const { SPECTATOR_GAMEMODE } = require('../constants/spectatorPackets');
const {
  POST_REPLAY_SETTLE_MS,
  replayPacketData,
  getPlayerChunkCenter,
  splitMiscReplayPackets,
  waitForClientTeleportConfirm,
} = require('./replayHelpers');
const { replayChunks } = require('./replayChunks');
const { minChunksForHandoff } = require('./replayHelpers');
const {
  replayCapturedTerrain,
  replayLooseCapturedMapChunks,
} = require('./replayTerrainCapture');
const {
  logProxyS2C,
  resetMapChunkReplayCount,
} = require('../utils/handoffTrace');

const log = createLogger('StateReplayer');

/**
 * Replays cached world state to a freshly connected client.
 * Sends packets in the correct order so the vanilla client initializes properly.
 */
class StateReplayer {
  /**
   * @param {import('../state/WorldStateCache').WorldStateCache} worldState
   * @param {import('../session/ServerConnection').ServerConnection} serverConn
   */
  constructor(worldState, serverConn) {
    this.worldState = worldState;
    this.serverConn = serverConn;
  }

  /**
   * Replay world state for a watch-only spectator client.
   * @param {object} client
   * @returns {Promise<void>}
   */
  async replaySpectator(client) {
    return this.replay(client, { spectator: true });
  }

  /**
   * Replay all cached state to the given client connection.
   * The client should be in the 'play' state already.
   *
   * @param {object} client - minecraft-protocol client connection (from proxy server)
   * @param {{ spectator?: boolean }} [options]
   * @returns {Promise<void>}
   */
  async replay(client, options = {}) {
    const spectator = options.spectator === true;
    const deferPostTerrain = options.deferPostTerrain === true;
    const ws = this.worldState;
    const bot = this.serverConn?.bot;
    const playerState = ws.player.getState();

    if (!playerState.loginPacket) {
      log.error('Cannot replay: no login packet cached');
      return;
    }

    log.info('Starting state replay...');
    resetMapChunkReplayCount();
    let packetCount = 0;
    let terrainMode = 'unknown';

    const write = (name, data) => {
      const payload = replayPacketData(client, name, data);
      if (payload !== data && data?.enforcesSecureChat) {
        log.info(`Replay ${name}: cleared enforcesSecureChat (proxy client has no profile keys)`);
      }
      logProxyS2C(log, name, payload, 'StateReplayer.write');
      try {
        client.write(name, payload);
        packetCount++;
      } catch (err) {
        log.error(`Failed to write packet '${name}':`, err.message);
      }
    };

    // 1. Login packet (join_game)
    write('login', { ...playerState.loginPacket });

    // 2. Difficulty
    if (playerState.difficulty) {
      write('difficulty', playerState.difficulty);
    }

    // 3. Abilities + permission level (entity_status 24–28 for game mode switcher)
    if (playerState.abilities) {
      let abilities = playerState.abilities;
      if (spectator && typeof abilities.flags === 'number') {
        abilities = { ...abilities, flags: abilities.flags | 0x0f };
      }
      write('abilities', abilities);
    }
    if (playerState.permissionStatus && !spectator) {
      write('entity_status', playerState.permissionStatus);
    }
    if (spectator) {
      write('game_state_change', { reason: 3, gameMode: SPECTATOR_GAMEMODE });
      if (playerState.entityId != null) {
        write('camera', { cameraId: playerState.entityId });
      }
    }

    const { beforeLevel: miscEarly, levelInfo: miscLevelInfo, weatherPackets } = splitMiscReplayPackets(
      ws.misc.getReplayPackets()
    );
    for (const pkt of miscEarly) {
      write(pkt.name, pkt.data);
    }

    // 4. held_item_slot (matches placeNewPlayer order)
    const invPackets = ws.inventory.getReplayPackets();
    const heldItemPackets = invPackets.filter(p => p.name === 'held_item_slot');
    const fullInvPackets = invPackets.filter(p => p.name !== 'held_item_slot');
    for (const pkt of heldItemPackets) {
      write(pkt.name, pkt.data);
    }

    // 5b. Recipes + advancements (ClientboundUpdateRecipesPacket, UpdateAdvancementsPacket)
    const joinPackets = ws.joinSync.getReplayPackets();
    if (joinPackets.length > 0) {
      log.info(`Replaying ${joinPackets.length} join sync packets (recipes/advancements)...`);
      for (const pkt of joinPackets) {
        write(pkt.name, pkt.data);
      }
    } else {
      log.warn('No recipes/advancements cached from server — client may log advancement load errors');
    }

    const center = getPlayerChunkCenter(playerState, ws.misc, bot);
    const serverVd = ws.misc.viewDistance?.viewDistance ?? 10;
    const botVd = this.serverConn?.config?.bot?.viewDistance ?? 10;
    const viewDistance = Math.min(serverVd, botVd);

    // 6. Teleport before terrain — PlayerList.placeNewPlayer teleports before sendLevelInfo/chunks
    const cachedPos = playerState.position;
    const teleportId = (cachedPos?.teleportId ?? 0) + 1;
    const initialPosition = bot?.entity?.position
      ? buildClientboundPositionPacket(bot, teleportId)
      : cachedPos;

    if (initialPosition) {
      write('position', initialPosition);
      log.info(
        `[Handoff] proxy S2C → java: position (replay teleport) teleportId=${initialPosition.teleportId}`,
      );
      await waitForClientTeleportConfirm(client);
      log.info('[Handoff] java teleport_confirm received after replay position');
    }

    // 8. sendLevelInfo — border, time, weather, spawn (PlayerList.sendLevelInfo)
    for (const pkt of miscLevelInfo) {
      write(pkt.name, pkt.data);
    }
    if (playerState.spawnPosition) {
      write('spawn_position', playerState.spawnPosition);
    }
    for (const pkt of weatherPackets) {
      write(pkt.name, pkt.data);
    }
    if (ws.misc.viewDistance) {
      write('update_view_distance', ws.misc.viewDistance);
    }

    const capturedTerrain = ws.getCapturedTerrainPackets();
    const useCapturedTerrain = capturedTerrain.length > 0 && ws.hasCapturedTerrainBatch();

    if (!useCapturedTerrain) {
      write('update_view_position', {
        chunkX: center.chunkX,
        chunkZ: center.chunkZ,
      });
      write('game_state_change', LEVEL_CHUNKS_LOAD_START);
    }

    if (useCapturedTerrain) {
      terrainMode = 'serverCapture';
      replayCapturedTerrain(client, capturedTerrain, write);
    } else {
      const loose = ws.getLooseTerrainMapChunks(center.chunkX, center.chunkZ, viewDistance);
      const minLoose = minChunksForHandoff(viewDistance);
      if (loose.length >= minLoose) {
        terrainMode = `looseCapture(${loose.length})`;
        log.info(`[Handoff] terrain path: ${terrainMode}`);
        replayLooseCapturedMapChunks(client, loose, write);
      } else {
        terrainMode = 'chunkCache';
        log.info(`[Handoff] terrain path: ${terrainMode}`);
        await this.replayCachedTerrain(client, {
          write,
          center,
          viewDistance,
        });
      }
    }
    if (terrainMode !== 'chunkCache') {
      log.info(`[Handoff] terrain path: ${terrainMode} (see map_chunk summary above)`);
    }

    if (!deferPostTerrain) {
      await this._replayPostTerrain(client, {
        write,
        ws,
        playerState,
        spectator,
        fullInvPackets,
      });
    }

    log.info(`State replay complete: ${packetCount} packets sent`);

    if (deferPostTerrain) {
      log.info(
        '[Handoff] deferPostTerrain: no settle wait — handoff replays entities/inventory next',
      );
    } else {
      log.info(`Waiting ${POST_REPLAY_SETTLE_MS}ms for client to render terrain...`);
      await new Promise((resolve) => setTimeout(resolve, POST_REPLAY_SETTLE_MS));
    }
  }

  /** Tab list, entities, inventory — after terrain is on the java client. */
  async replayPostTerrain(client, options = {}) {
    const spectator = options.spectator === true;
    const ws = this.worldState;
    const playerState = ws.player.getState();
    const invPackets = ws.inventory.getReplayPackets();
    const fullInvPackets = invPackets.filter((p) => p.name !== 'held_item_slot');

    let packetCount = 0;
    const write = (name, data) => {
      const payload = replayPacketData(client, name, data);
      logProxyS2C(log, name, payload, 'StateReplayer.postTerrain');
      try {
        client.write(name, payload);
        packetCount++;
      } catch (err) {
        log.error(`Failed to write packet '${name}':`, err.message);
      }
    };

    await this._replayPostTerrain(client, {
      write,
      ws,
      playerState,
      spectator,
      fullInvPackets,
    });
    log.info(`Post-terrain replay: ${packetCount} packets sent`);
  }

  async replayCachedTerrain(client, { write, center, viewDistance } = {}) {
    const ws = this.worldState;
    const writeFn =
      write ??
      ((name, data) => {
        client.write(name, replayPacketData(client, name, data));
      });

    writeFn('chunk_batch_start', {});
    const totalCached = ws.chunks.size;
    const chunks = ws.chunks.getChunksForReplay(center.chunkX, center.chunkZ, viewDistance);
    const sent = await replayChunks(client, writeFn, chunks, center, totalCached, viewDistance);
    writeFn('chunk_batch_finished', { batchSize: sent });
  }

  async _replayPostTerrain(
    client,
    { write, ws, playerState, spectator, fullInvPackets },
  ) {
    const playerInfoPackets = ws.misc.getPlayerInfoReplayPackets();
    if (playerInfoPackets.length > 0) {
      log.info(`Replaying ${playerInfoPackets.length} player_info packets (post-terrain)...`);
      for (const pkt of playerInfoPackets) {
        write(pkt.name, pkt.data);
      }
    }

    const waypointPackets = ws.misc.getWaypointReplayPackets();
    if (waypointPackets.length > 0) {
      log.info(`Replaying ${waypointPackets.length} tracked_waypoint packets (post-terrain)...`);
      for (const pkt of waypointPackets) {
        write(pkt.name, pkt.data);
      }
    }

    const entities = ws.entities.getAllEntities();
    log.info(`Replaying ${entities.length} entities...`);
    for (const entity of entities) {
      if (entity.entityId === playerState.entityId) continue;

      if (entity.spawnData) {
        write('spawn_entity', entity.spawnData);
      }
      if (entity.metadata) {
        write('entity_metadata', entity.metadata);
      }
      if (entity.equipment) {
        write('entity_equipment', entity.equipment);
      }
      for (const effect of entity.effects) {
        write('entity_effect', effect);
      }
      if (entity.passengers) {
        write('set_passengers', entity.passengers);
      }
    }

    // 11. Experience & health (final position sync is done in SessionManager after replay)
    if (playerState.experience) {
      write('experience', playerState.experience);
    }
    if (playerState.health) {
      write('update_health', playerState.health);
    }
    if (playerState.effects) {
      for (const effect of playerState.effects) {
        write('entity_effect', effect);
      }
    }

    // 12. Full inventory (spectators skip — watch-only)
    if (!spectator && fullInvPackets.length > 0) {
      log.info(`Replaying ${fullInvPackets.length} inventory packets...`);
      for (const pkt of fullInvPackets) {
        write(pkt.name, pkt.data);
      }
    }
  }
}

module.exports = { StateReplayer };

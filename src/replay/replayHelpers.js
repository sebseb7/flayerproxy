const TELEPORT_CONFIRM_TIMEOUT_MS = 15000;
const CHUNK_YIELD_EVERY = 32;
/** Vanilla keeps "Loading Terrain" at least ~2s after chunks start loading */
const POST_REPLAY_SETTLE_MS = 2500;

/**
 * Minimum in-view cached chunks before handoff replay (avoids "Loading Terrain" with 1 chunk).
 * @param {number} viewDistance
 */
function minChunksForHandoff(viewDistance) {
  const vd = Math.max(viewDistance ?? 10, 2);
  const radius = Math.min(vd, 4);
  return Math.max(9, (2 * radius + 1) ** 2);
}

function yieldEventLoop() {
  return new Promise((resolve) => setImmediate(resolve));
}

/**
 * Proxy clients without Mojang profile keys cannot satisfy enforcesSecureChat.
 * Strip the flag on replay so vanilla does not disable chat locally.
 */
function replayPacketData(client, name, data) {
  if (client.profileKeys || !data || typeof data !== 'object') return data;
  if ((name === 'login' || name === 'server_data') && data.enforcesSecureChat) {
    return { ...data, enforcesSecureChat: false };
  }
  return data;
}

function getPlayerChunkCenter(playerState, misc, bot) {
  if (bot?.entity?.position) {
    const p = bot.entity.position;
    return {
      chunkX: Math.floor(p.x / 16),
      chunkZ: Math.floor(p.z / 16),
    };
  }
  if (playerState.position) {
    return {
      chunkX: Math.floor(playerState.position.x / 16),
      chunkZ: Math.floor(playerState.position.z / 16),
    };
  }
  if (misc.viewPosition) {
    return {
      chunkX: misc.viewPosition.chunkX,
      chunkZ: misc.viewPosition.chunkZ,
    };
  }
  if (playerState.spawnPosition?.location) {
    const loc = playerState.spawnPosition.location;
    return {
      chunkX: Math.floor(loc.x / 16),
      chunkZ: Math.floor(loc.z / 16),
    };
  }
  return { chunkX: 0, chunkZ: 0 };
}

/** Split misc replay to match placeNewPlayer: HUD first, border/time after teleport */
function splitMiscReplayPackets(packets) {
  const beforeLevel = [];
  const levelInfo = [];
  const weatherPackets = [];
  for (const pkt of packets) {
    if (
      pkt.name === 'initialize_world_border' ||
      pkt.name === 'world_border_center' ||
      pkt.name === 'world_border_size' ||
      pkt.name === 'update_time'
    ) {
      levelInfo.push(pkt);
    } else if (
      pkt.name === 'game_state_change' &&
      pkt.data?.reason != null &&
      [1, 7, 8].includes(pkt.data.reason)
    ) {
      weatherPackets.push(pkt);
    } else if (pkt.name === 'update_view_distance') {
      continue;
    } else {
      beforeLevel.push(pkt);
    }
  }
  return { beforeLevel, levelInfo, weatherPackets };
}

function waitForClientTeleportConfirm(client) {
  return new Promise((resolve) => {
    if (!client || client.ended) return resolve();

    const timeout = setTimeout(() => {
      client.removeListener('teleport_confirm', onConfirm);
      resolve();
    }, TELEPORT_CONFIRM_TIMEOUT_MS);

    const onConfirm = () => {
      clearTimeout(timeout);
      resolve();
    };

    client.once('teleport_confirm', onConfirm);
  });
}

module.exports = {
  TELEPORT_CONFIRM_TIMEOUT_MS,
  CHUNK_YIELD_EVERY,
  POST_REPLAY_SETTLE_MS,
  minChunksForHandoff,
  yieldEventLoop,
  replayPacketData,
  getPlayerChunkCenter,
  splitMiscReplayPackets,
  waitForClientTeleportConfirm,
};

const conv = require('mineflayer/lib/conversions');

const ABSOLUTE_FLAGS = {
  x: false,
  y: false,
  z: false,
  yaw: false,
  pitch: false,
  dx: false,
  dy: false,
  dz: false,
  yawDelta: false,
};

/**
 * Build a clientbound position packet from the bot's live entity state.
 * @param {import('mineflayer').Bot} bot
 * @param {number} teleportId
 * @returns {object|null}
 */
function buildClientboundPositionPacket(bot, teleportId) {
  const entity = bot?.entity;
  if (!entity?.position) return null;

  return {
    teleportId,
    x: entity.position.x,
    y: entity.position.y,
    z: entity.position.z,
    dx: 0,
    dy: 0,
    dz: 0,
    yaw: conv.toNotchianYaw(entity.yaw),
    pitch: conv.toNotchianPitch(entity.pitch),
    flags: { ...ABSOLUTE_FLAGS },
  };
}

function waitForClientTeleportConfirm(client, timeoutMs, log) {
  return new Promise((resolve) => {
    if (!client || client.ended) return resolve(false);

    const timeout = setTimeout(() => {
      client.removeListener('teleport_confirm', onConfirm);
      if (log) log.warn('Timed out waiting for client teleport_confirm');
      resolve(false);
    }, timeoutMs);

    const onConfirm = () => {
      clearTimeout(timeout);
      resolve(true);
    };

    client.once('teleport_confirm', onConfirm);
  });
}

function movementFlags(onGround, hasHorizontalCollision) {
  return {
    onGround: !!onGround,
    hasHorizontalCollision: hasHorizontalCollision ?? false,
  };
}

/**
 * Serverbound position_look from bot entity (what the server expects).
 */
function buildServerboundPositionLook(bot) {
  const entity = bot?.entity;
  if (!entity?.position) return null;

  return {
    x: entity.position.x,
    y: entity.position.y,
    z: entity.position.z,
    yaw: conv.toNotchianYaw(entity.yaw),
    pitch: conv.toNotchianPitch(entity.pitch),
    flags: movementFlags(entity.onGround),
  };
}

function distanceSq(a, b) {
  const dx = a.x - b.x;
  const dy = a.y - b.y;
  const dz = a.z - b.z;
  return dx * dx + dy * dy + dz * dz;
}

/** Log when client diverges further than this from the bot (still forwarded to server) */
const MAX_CLIENT_MOVEMENT_WARN_DELTA = 12;

function chunkCoordsFromBlock(x, z) {
  return {
    chunkX: Math.floor(x / 16),
    chunkZ: Math.floor(z / 16),
  };
}

/**
 * Same test as ChunkTrackingView.isWithinDistance(..., includeNeighbors=false) on the server.
 */
function isChunkWithinViewDistance(centerChunkX, centerChunkZ, chunkX, chunkZ, viewDistance) {
  const bufferRange = 1;
  const deltaX = Math.max(0, Math.abs(chunkX - centerChunkX) - bufferRange);
  const deltaZ = Math.max(0, Math.abs(chunkZ - centerChunkZ) - bufferRange);
  return deltaX * deltaX + deltaZ * deltaZ < viewDistance * viewDistance;
}

/**
 * Keep the proxy client's chunk view center aligned so map_chunk packets are accepted.
 * @returns {boolean} true if the packet was sent
 */
function updateClientViewPosition(client, chunkX, chunkZ, lastView) {
  if (!client || client.ended || client.state !== 'play') return false;
  if (lastView && lastView.chunkX === chunkX && lastView.chunkZ === chunkZ) return false;

  try {
    client.write('update_view_position', { chunkX, chunkZ });
  } catch {
    return false;
  }
  if (lastView) {
    lastView.chunkX = chunkX;
    lastView.chunkZ = chunkZ;
  }
  return true;
}

/**
 * Move view center to the player's chunk if missing or a chunk would be rejected.
 * @returns {boolean} true if update_view_position was sent
 */
function ensureClientViewIncludesChunk(client, playerBlockX, playerBlockZ, chunkX, chunkZ, viewDistance, lastView) {
  if (!client || client.ended || client.state !== 'play') return false;

  const playerChunk = chunkCoordsFromBlock(playerBlockX, playerBlockZ);

  if (lastView?.chunkX == null) {
    return updateClientViewPosition(client, playerChunk.chunkX, playerChunk.chunkZ, lastView);
  }

  if (!isChunkWithinViewDistance(lastView.chunkX, lastView.chunkZ, chunkX, chunkZ, viewDistance)) {
    return updateClientViewPosition(client, playerChunk.chunkX, playerChunk.chunkZ, lastView);
  }

  return false;
}

module.exports = {
  buildClientboundPositionPacket,
  buildServerboundPositionLook,
  waitForClientTeleportConfirm,
  movementFlags,
  distanceSq,
  MAX_CLIENT_MOVEMENT_WARN_DELTA,
  chunkCoordsFromBlock,
  isChunkWithinViewDistance,
  updateClientViewPosition,
  ensureClientViewIncludesChunk,
  ABSOLUTE_FLAGS,
};

const { createLogger } = require('../utils/logger');
const conv = require('mineflayer/lib/conversions');
const {
  buildClientboundPositionPacket,
  buildServerboundFromClientboundPosition,
  buildServerboundPositionLook,
  waitForClientTeleportConfirm,
  movementFlags,
  distanceSq,
  MAX_CLIENT_MOVEMENT_WARN_DELTA,
} = require('../utils/positionSync');

const log = createLogger('ServerConn');

/**
 * Apply a proxy client's movement packet to the bot entity, then send serverbound packets
 * using the client's coordinates so ChunkMap.move() tracks where the player walks.
 * @param {import('mineflayer').Bot} bot
 * @param {object} rawClient - minecraft-protocol client
 * @param {string} name - packet name
 * @param {object} data - packet data
 * @returns {boolean} false only when the bot entity is not ready
 */
/**
 * @param {object} [opts]
 * @param {boolean} [opts.syncEntity=true] — false: passthrough java client packets verbatim
 */
function relayClientMovement(bot, rawClient, name, data, opts = {}) {
  const syncEntity = opts.syncEntity !== false;

  if (!syncEntity) {
    try {
      rawClient.write(name, data);
      return true;
    } catch (err) {
      log.error(`Failed to relay movement '${name}':`, err.message);
      return false;
    }
  }

  if (!bot?.entity?.position) return false;

  const entity = bot.entity;

  if (name === 'position' || name === 'position_look') {
    const target = { x: data.x, y: data.y, z: data.z };
    const dist = Math.sqrt(distanceSq(target, entity.position));
    if (dist > MAX_CLIENT_MOVEMENT_WARN_DELTA) {
      log.warn(
        `Client ${dist.toFixed(1)} blocks ahead of bot entity — forwarding anyway (syncEntity=false skips entity write)`,
      );
    }
    entity.position.set(target.x, target.y, target.z);
  }

  if (name === 'position_look' || name === 'look') {
    if (data.yaw !== undefined) entity.yaw = conv.fromNotchianYaw(data.yaw);
    if (data.pitch !== undefined) entity.pitch = conv.fromNotchianPitch(data.pitch);
  }

  const onGround = data.onGround ?? data.flags?.onGround;
  if (onGround !== undefined) entity.onGround = onGround;

  const flags = movementFlags(
    onGround ?? (syncEntity ? entity.onGround : false),
    data.flags?.hasHorizontalCollision,
  );

  try {
    log.debug(`[java-client] upstream ${name}`);
    if (name === 'flying' && data.x === undefined) {
      rawClient.write('flying', { flags });
    } else if (name === 'look') {
      rawClient.write('look', {
        yaw: data.yaw ?? conv.toNotchianYaw(entity.yaw),
        pitch: data.pitch ?? conv.toNotchianPitch(entity.pitch),
        flags,
      });
    } else if (name === 'position') {
      rawClient.write('position', {
        x: data.x,
        y: data.y,
        z: data.z,
        flags,
      });
    } else if (name === 'position_look') {
      rawClient.write('position_look', {
        x: data.x,
        y: data.y,
        z: data.z,
        yaw: data.yaw,
        pitch: data.pitch,
        flags,
      });
    } else {
      rawClient.write(name, data);
    }
    return true;
  } catch (err) {
    log.error(`Failed to relay movement '${name}':`, err.message);
    return false;
  }
}

/**
 * Snap the proxy client to the bot's current server-side position.
 * Call after replay and before enabling movement forwarding.
 * @param {import('mineflayer').Bot} bot
 * @param {{ player: import('../state/PlayerStateCache').PlayerStateCache }} worldState
 * @param {object} client - minecraft-protocol client
 * @returns {Promise<boolean>}
 */
async function syncProxyClientPosition(bot, worldState, client, serverConn) {
  if (!bot?.entity?.position) {
    log.warn('Cannot sync client position: bot entity not ready');
    return false;
  }

  const cached = worldState.player.position;
  const teleportId = (cached?.teleportId ?? 0) + 1;
  const packet = buildClientboundPositionPacket(bot, teleportId);
  if (!packet) return false;

  const { x, y, z } = bot.entity.position;
  const chunkX = Math.floor(x / 16);
  const chunkZ = Math.floor(z / 16);

  try {
    client.write('position', packet);
    worldState.player.handlePosition(packet);
  } catch (err) {
    log.error('Failed to write position sync to client:', err.message);
    return false;
  }

  const confirmId = await waitForClientTeleportConfirm(client, 10000, log);
  if (confirmId !== false && serverConn) {
    serverConn.writeToServer(
      'teleport_confirm',
      {
        teleportId: confirmId === true ? packet.teleportId : confirmId,
      },
      { source: 'MovementRelay.forwardJavaTeleportConfirm' },
    );
    log.info(
      `Forwarded java-client teleport_confirm id=${confirmId === true ? packet.teleportId : confirmId} after position sync`,
    );
  }

  try {
    client.write('update_view_position', { chunkX, chunkZ });
  } catch (err) {
    log.error('Failed to write update_view_position after sync:', err.message);
  }

  log.info(
    `Synced client to bot position (${x.toFixed(2)}, ${y.toFixed(2)}, ${z.toFixed(2)}) chunk (${chunkX}, ${chunkZ})`
  );
  return true;
}

/**
 * Tell the server the bot's current position (serverbound position_look).
 * @param {import('mineflayer').Bot} bot
 * @param {object} rawClient - minecraft-protocol client
 * @param {boolean} connected
 * @returns {boolean}
 */
function confirmServerPosition(bot, rawClient, connected) {
  if (!rawClient || !connected) return false;

  const packet = buildServerboundPositionLook(bot);
  if (!packet) return false;

  try {
    rawClient.write('position_look', packet);
    const { x, y, z } = bot.entity.position;
    log.info(`Confirmed server position (${x.toFixed(2)}, ${y.toFixed(2)}, ${z.toFixed(2)})`);
    return true;
  } catch (err) {
    log.error('Failed to confirm server position:', err.message);
    return false;
  }
}

/**
 * Grim setback: server needs teleport_confirm + matching position_look on the bot connection.
 * Do not forward these S2C packets to the java client (causes duplicate position_look / AimDuplicateLook).
 * @param {import('mineflayer').Bot} bot
 * @param {import('./ServerConnection').ServerConnection} serverConn
 * @param {object} data - clientbound position
 */
function acceptGrimSetbackOnUpstream(bot, serverConn, data) {
  const entity = bot?.entity;
  if (!entity?.position) return false;

  serverConn.writeToServer('teleport_confirm', { teleportId: data.teleportId });
  const packet = buildServerboundFromClientboundPosition(bot, data);
  if (!packet) return false;

  serverConn.writeToServer('position_look', packet);
  entity.position.set(packet.x, packet.y, packet.z);
  entity.yaw = conv.fromNotchianYaw(packet.yaw);
  entity.pitch = conv.fromNotchianPitch(packet.pitch);
  entity.onGround = packet.flags?.onGround ?? true;

  log.info(
    `[proxy] Grim setback id=${data.teleportId} → confirm + position_look (${packet.x.toFixed(2)}, ${packet.y.toFixed(2)}, ${packet.z.toFixed(2)})`,
  );
  return true;
}

module.exports = {
  relayClientMovement,
  syncProxyClientPosition,
  confirmServerPosition,
  acceptGrimSetbackOnUpstream,
};

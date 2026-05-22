/**
 * Mineflayer omits serverbound tick_end (1.21.2+). Inject only in BOT_MODE.
 * In CLIENT_MODE the Java client sends its own tick_end — injecting here duplicates
 * packets and triggers Grim TickTimer / packet-spam kicks.
 */

const TICK_END = 'tick_end';
const POSITION_REMINDER_AFTER_FLYING = 18;

const CLIENT_INPUT = new Set([
  'position',
  'position_look',
  'look',
  'flying',
  'entity_action',
  'player_input',
  'animation',
  'arm_animation',
]);

/** Mineflayer must not emit these when the Java client drives the same upstream connection */
/** Java client is the only writer for these while CLIENT_MODE (mineflayer shares bot._client) */
const MINEFLAYER_GHOST_WHEN_CLIENT_DRIVES = new Set([
  'position',
  'position_look',
  'look',
  'flying',
  'vehicle_move',
  'steer_vehicle',
  'paddle_boat',
  'tick_end',
  'teleport_confirm',
]);

/**
 * @param {string} version
 * @returns {boolean}
 */
function supportsTickEnd(version) {
  const m = String(version).match(/^(\d+)\.(\d+)(?:\.(\d+))?/);
  if (!m) return false;
  const major = Number(m[1]);
  const minor = Number(m[2]);
  const patch = Number(m[3] ?? 0);
  if (major !== 1) return major > 1;
  if (minor > 21) return true;
  if (minor < 21) return false;
  return patch >= 2;
}

function writePositionReminder(bot, client, origWrite) {
  const entity = bot.entity;
  if (!entity?.position || client.ended || client.state !== 'play') return;
  const onGround = !!entity.onGround;
  origWrite('position', {
    x: entity.position.x,
    y: entity.position.y,
    z: entity.position.z,
    onGround,
    flags: { onGround, hasHorizontalCollision: undefined },
  });
}

/**
 * @param {import('mineflayer').Bot} bot
 * @param {string} version
 * @param {{ isBotMode: () => boolean, isBridgeRelay: () => boolean, ghostBlocked?: { counts: Map<string, number> } }} hooks
 * @returns {() => void}
 */
function installTickEndRelay(bot, version, hooks) {
  if (!supportsTickEnd(version)) return () => {};

  const client = bot._client;
  const origWrite = client.write.bind(client);
  let pendingTickEnd = null;
  let relaying = false;
  let flyingWithoutPosition = 0;

  const flushTickEnd = () => {
    pendingTickEnd = null;
    if (!hooks.isBotMode() || client.ended || client.state !== 'play') return;
    origWrite(TICK_END, {});
  };

  const scheduleTickEnd = () => {
    if (!hooks.isBotMode() || pendingTickEnd) return;
    pendingTickEnd = setImmediate(flushTickEnd);
  };

  client.write = (name, params) => {
    if (
      !hooks.isBridgeRelay() &&
      !hooks.isBotMode() &&
      MINEFLAYER_GHOST_WHEN_CLIENT_DRIVES.has(name)
    ) {
      if (hooks.ghostBlocked?.counts) {
        const key = `mineflayer-physics:${name}`;
        hooks.ghostBlocked.counts.set(
          key,
          (hooks.ghostBlocked.counts.get(key) || 0) + 1,
        );
      }
      return;
    }

    const result = origWrite(name, params);

    if (
      relaying ||
      !hooks.isBotMode() ||
      client.state !== 'play' ||
      name === TICK_END ||
      !CLIENT_INPUT.has(name)
    ) {
      return result;
    }

    relaying = true;
    try {
      if (name === 'position' || name === 'position_look') {
        flyingWithoutPosition = 0;
      } else if (name === 'flying' || name === 'look') {
        flyingWithoutPosition += 1;
        if (flyingWithoutPosition >= POSITION_REMINDER_AFTER_FLYING) {
          flyingWithoutPosition = 0;
          writePositionReminder(bot, client, origWrite);
        }
      }
      scheduleTickEnd();
    } finally {
      relaying = false;
    }

    return result;
  };

  return () => {
    if (pendingTickEnd) clearImmediate(pendingTickEnd);
    client.write = origWrite;
  };
}

module.exports = { supportsTickEnd, installTickEndRelay };

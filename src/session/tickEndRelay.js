/**
 * Vanilla 1.21.2+ sends CLIENT_TICK_END once per client tick, after movement/input.
 * Mineflayer never sends it; Grim TickTimer flags every flying without a prior tick_end
 * (see Grim/common/.../checks/impl/timer/TickTimer.java).
 *
 * We append tick_end synchronously after each serverbound activity packet via origWrite
 * so packet order on the wire is: … → flying → tick_end → flying → tick_end.
 */

const TICK_END = 'tick_end';

/** Packets that end a client tick (Grim PacketOrderO: nothing else after flying until tick_end) */
const TICK_ACTIVITY = new Set([
  'position',
  'position_look',
  'look',
  'flying',
  'entity_action',
  'player_input',
  'animation',
  'arm_animation',
]);

/** Grim BadPacketsE: >19 flying without position on 1.21.2+ */
const POSITION_REMINDER_AFTER_FLYING = 18;

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

/**
 * @param {import('mineflayer').Bot} bot
 * @param {import('minecraft-protocol').Client} client
 * @param {typeof client.write} origWrite
 */
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
 * @returns {() => void} cleanup
 */
function installTickEndRelay(bot, version) {
  if (!supportsTickEnd(version)) return () => {};

  const client = bot._client;
  const origWrite = client.write.bind(client);
  let flyingWithoutPosition = 0;
  let relaying = false;

  const sendTickEnd = () => {
    if (client.ended || client.state !== 'play') return;
    origWrite(TICK_END, {});
  };

  client.write = (name, params) => {
    const result = origWrite(name, params);

    if (relaying || client.state !== 'play' || name === TICK_END || !TICK_ACTIVITY.has(name)) {
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
      sendTickEnd();
    } catch {
      /* socket closing */
    } finally {
      relaying = false;
    }

    return result;
  };

  return () => {
    client.write = origWrite;
  };
}

module.exports = { supportsTickEnd, installTickEndRelay };

const { createLogger } = require('./logger');

const log = createLogger('Upstream');

const MOVEMENT = new Set([
  'flying',
  'position',
  'position_look',
  'look',
  'tick_end',
  'vehicle_move',
  'steer_vehicle',
  'paddle_boat',
]);

/**
 * Observe serverbound movement writes (install after tickEndRelay).
 * @param {object} client - minecraft-protocol client (bot._client)
 * @param {{ isBridgeRelay: () => boolean, isBotMode: () => boolean, ghostBlocked: { counts: Map<string, number> } }} hooks
 * @param {{ enabled?: boolean }} [opts]
 * @returns {() => void}
 */
function installUpstreamMovementLog(client, hooks, opts = {}) {
  if (opts.enabled === false) return () => {};

  const origWrite = client.write.bind(client);
  let windowStart = Date.now();
  const sent = new Map();

  const flush = () => {
    const elapsed = (Date.now() - windowStart) / 1000;
    if (elapsed < 1) return;

    const parts = [...sent.entries()]
      .sort((a, b) => b[1] - a[1])
      .map(([k, v]) => `${k}:${v}`)
      .join(' ');

    const blockedParts = [...hooks.ghostBlocked.counts.entries()]
      .sort((a, b) => b[1] - a[1])
      .map(([k, v]) => `${k}:${v}`)
      .join(' ');

    const blocked =
      blockedParts.length > 0 ? ` | swallowed-bot=${blockedParts}` : '';

    log.info(`S2U ${elapsed.toFixed(1)}s — ${parts || 'none'}${blocked}`);

    const moveBridge =
      (sent.get('flying(bridge)') || 0) +
      (sent.get('position_look(bridge)') || 0) +
      (sent.get('position(bridge)') || 0) +
      (sent.get('look(bridge)') || 0);
    const tickBridge = sent.get('tick_end(bridge)') || 0;
    if (tickBridge > 0 && moveBridge > tickBridge) {
      log.warn(
        `[Grim TickTimer] S2U movement(${moveBridge}) > tick_end(${tickBridge}) in window`,
      );
    }

    sent.clear();
    hooks.ghostBlocked.counts.clear();
    windowStart = Date.now();
  };

  const timer = setInterval(flush, 3000);

  client.write = (name, params) => {
    if (MOVEMENT.has(name)) {
      const src = hooks.isBridgeRelay()
        ? 'bridge'
        : hooks.isBotMode()
          ? 'bot'
          : 'other';
      const key = `${name}(${src})`;
      sent.set(key, (sent.get(key) || 0) + 1);
    }
    return origWrite(name, params);
  };

  log.info('Upstream movement trace ON (every 3s; set bot.logMovement=false to disable)');

  return () => {
    clearInterval(timer);
    flush();
    client.write = origWrite;
  };
}

module.exports = { installUpstreamMovementLog };

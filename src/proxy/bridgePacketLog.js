const { createLogger } = require('../utils/logger');

const log = createLogger('Bridge');

const MOVEMENT = new Set([
  'flying',
  'position',
  'position_look',
  'look',
  'tick_end',
]);

/**
 * C2S movement tracing aligned with Grim checks in ./Grim:
 * - TickTimer: >1 flying packet before CLIENT_TICK_END
 * - AimDuplicateLook: RotationUpdate.from.equals(to) on consecutive look
 * - BadPacketsN: SetbackTeleportUtil — movement after teleport txn without matching queue
 */
class BridgePacketLog {
  constructor() {
    this._burst = [];
    this._windowStart = Date.now();
    this._counts = new Map();
    this._droppedWhileBlocked = 0;
    this._maxBurstLen = 0;
  }

  recordC2SDropped(name) {
    if (!MOVEMENT.has(name)) return;
    this._droppedWhileBlocked += 1;
  }

  /**
   * Raw client packet (before coalesce).
   * @param {string} name
   * @param {object} [data]
   */
  recordC2S(name, data) {
    if (!MOVEMENT.has(name)) return;

    this._counts.set(name, (this._counts.get(name) || 0) + 1);

    if (name === 'tick_end') {
      this._onTickEnd(data);
      return;
    }

    const tag = this._tagMovement(name, data);
    this._burst.push(tag);
    if (this._burst.length > this._maxBurstLen) {
      this._maxBurstLen = this._burst.length;
    }
  }

  _tagMovement(name, data) {
    if (name === 'look' || name === 'position_look') {
      const y = data?.yaw;
      const p = data?.pitch;
      if (y != null && p != null) {
        return `${name}(y=${y.toFixed(2)},p=${p.toFixed(2)})`;
      }
    }
    if (name === 'position' || name === 'position_look') {
      const x = data?.x;
      const y = data?.y;
      const z = data?.z;
      if (x != null) {
        return `${name}(${x.toFixed(2)},${y?.toFixed(2)},${z?.toFixed(2)})`;
      }
    }
    const g = data?.onGround ?? data?.flags?.onGround;
    if (g !== undefined) return `${name}(g=${g})`;
    return name;
  }

  _onTickEnd() {
    const n = this._burst.length;
    if (n > 1) {
      log.info(
        `[vanilla 1.21 C2S] ${n} movement packets before tick_end: ${this._burst.join(' → ')} — forwarding all (same as direct connect)`,
      );
    }
    this._burst = [];
  }

  noteS2CTeleport(data, source = 'server-via-bot') {
    if (!data) return;
    const id = data.teleportId;
    const pos =
      data.x != null
        ? `(${Number(data.x).toFixed(2)}, ${Number(data.y).toFixed(2)}, ${Number(data.z).toFixed(2)})`
        : '(relative/flags)';
    log.info(
      `[Grim BadPacketsN] S2C position from ${source} teleportId=${id} ${pos} ` +
        `flags=${JSON.stringify(data.flags ?? {})} — java-client must send teleport_confirm + matching movement`,
    );
  }

  flushSummary() {
    const elapsed = (Date.now() - this._windowStart) / 1000;
    if (elapsed < 1) return;

    const parts = [...this._counts.entries()]
      .sort((a, b) => b[1] - a[1])
      .map(([k, v]) => `${k}:${v}`)
      .join(' ');

    const dropped =
      this._droppedWhileBlocked > 0
        ? ` | dropped(pre-sync)=${this._droppedWhileBlocked}`
        : '';
    log.info(
      `C2S ${elapsed.toFixed(1)}s — ${parts || 'no movement'} | max burst before tick_end=${this._maxBurstLen}${dropped}`,
    );

    this._counts.clear();
    this._droppedWhileBlocked = 0;
    this._maxBurstLen = 0;
    this._windowStart = Date.now();
  }
}

module.exports = { BridgePacketLog };

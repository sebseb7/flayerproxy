const { createLogger } = require('../utils/logger');

const log = createLogger('BotIdle');

const ACTIONS = ['look', 'sneak', 'swing'];

/**
 * Random look / sneak / swing while the bot holds the session (no Java client).
 */
class BotIdleBehavior {
  /**
   * @param {import('mineflayer').Bot} bot
   * @param {object} botConfig - config.bot
   * @param {{ onSwing?: (hand: 'left'|'right') => void }} [hooks]
   */
  constructor(bot, botConfig, hooks = {}) {
    this.bot = bot;
    this.config = botConfig;
    this.onSwing = hooks.onSwing;
    this._enabled = false;
    this._timer = null;
    this._sneakReleaseTimer = null;
    this._sneaking = false;
  }

  start() {
    if (!this.config.antiAfk || this._enabled) return;
    this._enabled = true;
    log.debug('Idle behavior started');
    this._scheduleNext();
  }

  stop() {
    this._enabled = false;
    if (this._timer) {
      clearTimeout(this._timer);
      this._timer = null;
    }
    if (this._sneakReleaseTimer) {
      clearTimeout(this._sneakReleaseTimer);
      this._sneakReleaseTimer = null;
    }
    this._releaseSneak();
  }

  _scheduleNext() {
    if (!this._enabled) return;
    const min = this.config.antiAfkMinInterval ?? 1500;
    const max = this.config.antiAfkMaxInterval ?? this.config.antiAfkInterval ?? 6000;
    const lo = Math.min(min, max);
    const hi = Math.max(min, max);
    const delay = lo + Math.random() * (hi - lo);
    this._timer = setTimeout(() => {
      this._timer = null;
      this._tick().finally(() => this._scheduleNext());
    }, delay);
  }

  async _tick() {
    if (!this._enabled || !this.bot?.entity) return;

    const action = ACTIONS[Math.floor(Math.random() * ACTIONS.length)];
    try {
      if (action === 'look') await this._randomLook();
      else if (action === 'sneak') this._randomSneak();
      else if (action === 'swing') this._randomSwing();
    } catch (err) {
      log.debug(`Idle ${action} skipped: ${err.message}`);
    }
  }

  async _randomLook() {
    const { yaw, pitch } = this.bot.entity;
    const newYaw = yaw + (Math.random() - 0.5) * Math.PI;
    const newPitch = Math.max(-1.2, Math.min(1.2, pitch + (Math.random() - 0.5) * 0.6));
    await this.bot.look(newYaw, newPitch, false);
  }

  _randomSneak() {
    if (this._sneaking) {
      this._releaseSneak();
      return;
    }
    this.bot.setControlState('sneak', true);
    this._sneaking = true;
    const holdMs = 400 + Math.random() * 2000;
    this._sneakReleaseTimer = setTimeout(() => {
      this._sneakReleaseTimer = null;
      this._releaseSneak();
    }, holdMs);
  }

  _releaseSneak() {
    if (!this._sneaking) return;
    try {
      this.bot.setControlState('sneak', false);
    } catch {
      /* bot may be gone */
    }
    this._sneaking = false;
  }

  _randomSwing() {
    const hand = Math.random() < 0.5 ? 'right' : 'left';
    this.bot.swingArm(hand);
    this.onSwing?.(hand);
  }
}

module.exports = { BotIdleBehavior };

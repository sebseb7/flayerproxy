const { createLogger } = require('../utils/logger');

const log = createLogger('AutoLogout');

/**
 * Disconnect the bot when configured triggers fire (bot mode only).
 */
class BotAutoLogout {
  /**
   * @param {import('mineflayer').Bot} bot
   * @param {object} botConfig - config.bot
   * @param {() => boolean} isActive - true when bot holds the session (not handoff / client mode)
   * @param {(reason: string) => void} onLogout
   * @param {string} [authUsername] - config auth username (always allowed)
   */
  constructor(bot, botConfig, isActive, onLogout, authUsername) {
    this.bot = bot;
    this.config = botConfig;
    this.isActive = isActive;
    this.onLogout = onLogout;
    this._enabled = false;
    this._triggered = false;
    this._selfName = (bot.username || authUsername || '').toLowerCase();
    const allowed = [...(botConfig.autoLogout?.allowedPlayers || [])];
    if (authUsername) allowed.push(authUsername);
    this._allowed = this._normalizeAllowed(allowed);
    this._onEntityHurt = (entity) => this._handleEntityHurt(entity);
    this._onEntitySpawn = (entity) => this._handleEntitySpawn(entity);
    this._onMove = () => this._checkBelowY();
  }

  _belowYThreshold() {
    const v = this._autoLogoutConfig().belowY;
    if (v === false || v == null) return null;
    const n = Number(v);
    return Number.isFinite(n) ? n : null;
  }

  _normalizeAllowed(list) {
    const names = new Set(
      (list || []).map((n) => String(n).trim().toLowerCase()).filter(Boolean),
    );
    if (this._selfName) names.add(this._selfName);
    return names;
  }

  _autoLogoutConfig() {
    return this.config.autoLogout || {};
  }

  _isConfigured() {
    const cfg = this._autoLogoutConfig();
    if (cfg.enabled === false) return false;
    return cfg.onDamage || cfg.onPlayer || this._belowYThreshold() != null;
  }

  start() {
    if (!this._isConfigured() || this._enabled) return;
    this._enabled = true;
    this._triggered = false;
    const belowY = this._belowYThreshold();
    log.info(
      `Auto logout armed (damage=${!!this._autoLogoutConfig().onDamage}, ` +
        `player=${!!this._autoLogoutConfig().onPlayer}, belowY=${belowY ?? 'off'}, ` +
        `allowed=${this._allowed.size})`,
    );
    this.bot.on('entityHurt', this._onEntityHurt);
    this.bot.on('entitySpawn', this._onEntitySpawn);
    if (belowY != null) this.bot.on('move', this._onMove);
    this._scanExistingPlayerEntities();
    this._checkBelowY();
  }

  stop() {
    if (!this._enabled) return;
    this._enabled = false;
    this.bot.removeListener('entityHurt', this._onEntityHurt);
    this.bot.removeListener('entitySpawn', this._onEntitySpawn);
    this.bot.removeListener('move', this._onMove);
  }

  _scanExistingPlayerEntities() {
    if (!this._autoLogoutConfig().onPlayer) return;
    for (const entity of Object.values(this.bot.entities || {})) {
      if (entity?.type !== 'player' || !entity.username) continue;
      this._handlePlayerSeen(entity.username);
    }
  }

  _handleEntityHurt(entity) {
    if (!this._autoLogoutConfig().onDamage) return;
    if (!this.isActive() || this._triggered) return;
    if (entity !== this.bot.entity) return;
    this._trigger('damage');
  }

  _handleEntitySpawn(entity) {
    if (!this._autoLogoutConfig().onPlayer) return;
    if (!this.isActive() || this._triggered) return;
    if (entity?.type !== 'player' || !entity.username) return;
    this._handlePlayerSeen(entity.username);
  }

  _checkBelowY() {
    const threshold = this._belowYThreshold();
    if (threshold == null) return;
    if (!this.isActive() || this._triggered) return;
    const y = this.bot.entity?.position?.y;
    if (y == null || !Number.isFinite(y) || y >= threshold) return;
    this._trigger('belowY');
  }

  _handlePlayerSeen(username) {
    if (!this._autoLogoutConfig().onPlayer) return;
    if (!this.isActive() || this._triggered) return;
    if (!username) return;

    const name = String(username).trim().toLowerCase();
    if (!name || this._allowed.has(name)) return;

    this._trigger(`player:${username}`);
  }

  _trigger(kind) {
    if (this._triggered) return;
    this._triggered = true;
    this.stop();
    let label;
    if (kind === 'damage') label = 'took damage';
    else if (kind === 'belowY') label = `Y below ${this._belowYThreshold()}`;
    else label = `player detected (${kind.replace(/^player:/, '')})`;
    log.warn(`Auto logout — ${label}`);
    this.onLogout(label);
  }
}

module.exports = { BotAutoLogout };

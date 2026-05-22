const { createLogger } = require('../utils/logger');
const log = createLogger('PlayerStateCache');

/** ClientboundEntityEventPacket — PlayerList.sendPlayerPermissionLevel (EntityEvent.java) */
const PERMISSION_STATUS_MIN = 24;
const PERMISSION_STATUS_MAX = 28;

/**
 * Caches player-specific state: position, health, XP, abilities, gamemode.
 */
class PlayerStateCache {
  constructor() {
    this.loginPacket = null;      // The login/join_game packet
    this.position = null;         // { x, y, z, yaw, pitch, flags, teleportId }
    this.health = null;           // { health, food, foodSaturation }
    this.experience = null;       // { experienceBar, level, totalExperience }
    this.abilities = null;        // { flags, flyingSpeed, walkingSpeed }
    this.spawnPosition = null;    // { location, angle }
    this.gameMode = null;         // from game_state_change
    this.difficulty = null;       // { difficulty, difficultyLocked }
    this.entityId = null;         // The player's entity ID from login
    /** entity_status for game-mode switcher / command permission UI */
    this.permissionStatus = null; // { entityId, entityStatus }
    this.effects = [];            // player status effects
  }

  handleLogin(data) {
    this.loginPacket = { ...data };
    this.entityId = data.entityId;
    log.info(`Player entity ID: ${this.entityId}`);
  }

  handlePosition(data) {
    this.position = { ...data };
  }

  handleUpdateHealth(data) {
    this.health = { ...data };
  }

  handleExperience(data) {
    this.experience = { ...data };
  }

  handleAbilities(data) {
    this.abilities = { ...data };
  }

  /**
   * Permission level for the local player (entity_status 24–28).
   */
  handleEntityStatus(data) {
    if (this.entityId == null || data.entityId !== this.entityId) return;
    const status = data.entityStatus ?? data.status;
    if (status == null || status < PERMISSION_STATUS_MIN || status > PERMISSION_STATUS_MAX) return;
    this.permissionStatus = {
      entityId: data.entityId,
      entityStatus: status,
    };
    log.info(`Cached permission level entity_status: ${status}`);
  }

  handleSpawnPosition(data) {
    this.spawnPosition = { ...data };
  }

  handleDifficulty(data) {
    this.difficulty = { ...data };
  }

  handleGameStateChange(data) {
    // reason 3 = change game mode
    if (data.reason === 3) {
      this.gameMode = data.gameMode;
    }
  }

  handleRespawn(data) {
    // On respawn, reset some state
    this.position = null;
    this.health = null;
    this.effects = [];
    log.info('Player respawned — position/health/effects reset');
  }

  handleEntityEffect(data) {
    if (this.entityId == null || data.entityId !== this.entityId) return;
    this.effects = this.effects.filter(e => e.effectId !== data.effectId);
    this.effects.push({ ...data });
  }

  handleRemoveEntityEffect(data) {
    if (this.entityId == null || data.entityId !== this.entityId) return;
    this.effects = this.effects.filter(e => e.effectId !== data.effectId);
  }

  /**
   * Returns all cached player state for replay.
   */
  getState() {
    return {
      loginPacket: this.loginPacket,
      position: this.position,
      health: this.health,
      experience: this.experience,
      abilities: this.abilities,
      spawnPosition: this.spawnPosition,
      difficulty: this.difficulty,
      entityId: this.entityId,
      permissionStatus: this.permissionStatus,
      effects: this.effects,
    };
  }

  clear() {
    this.loginPacket = null;
    this.position = null;
    this.health = null;
    this.experience = null;
    this.abilities = null;
    this.spawnPosition = null;
    this.gameMode = null;
    this.difficulty = null;
    this.entityId = null;
    this.permissionStatus = null;
    this.effects = [];
  }
}

module.exports = {
  PlayerStateCache,
  PERMISSION_STATUS_MIN,
  PERMISSION_STATUS_MAX,
};

const { createLogger } = require('../utils/logger');
const { ScoreboardCache } = require('./ScoreboardCache');
const { WorldBorderCache } = require('./WorldBorderCache');

const log = createLogger('MiscCache');

/**
 * Caches miscellaneous world state:
 * time, weather, world border, scoreboard, boss bars, tab list, tags, etc.
 */
class MiscCache {
  constructor() {
    // Time
    this.time = null;

    // Weather (from game_state_change: reason 1 = begin rain, 2 = end rain,
    // 7 = rain level, 8 = thunder level)
    this.weather = {
      raining: false,
      rainLevel: null,
      thunderLevel: null,
    };

    // World border (delegated)
    this._worldBorder = new WorldBorderCache();

    // Tab list
    this.playerInfo = new Map();        // UUID -> merged player_info data
    /** @type {Array<{action: object, data: object[]}>} verbatim packets for replay */
    this.playerInfoPackets = [];
    this.playerListHeader = null;       // playerlist_header

    // Scoreboard (delegated)
    this._scoreboard = new ScoreboardCache();

    // Boss bars
    this.bossBars = new Map();  // UUID -> boss_bar data

    // Tags
    this.tags = null;

    // Server data
    this.serverData = null;

    // Simulation distance / view distance
    this.simulationDistance = null;
    this.viewDistance = null;

    // Declare commands
    this.declareCommands = null;

    // Update view position
    this.viewPosition = null;

    // Player remove tracking
    this.removedPlayers = new Set();
  }

  handleUpdateTime(data) {
    this.time = { ...data };
  }

  handleGameStateChange(data) {
    switch (data.reason) {
      case 1: // Begin rain
        this.weather.raining = true;
        break;
      case 2: // End rain
        this.weather.raining = false;
        break;
      case 7: // Rain level
        this.weather.rainLevel = data.gameMode;
        break;
      case 8: // Thunder level
        this.weather.thunderLevel = data.gameMode;
        break;
    }
  }

  // World border packets — delegate to WorldBorderCache
  handleInitWorldBorder(data) { this._worldBorder.handleInitWorldBorder(data); }
  handleWorldBorderCenter(data) { this._worldBorder.handleWorldBorderCenter(data); }
  handleWorldBorderSize(data) { this._worldBorder.handleWorldBorderSize(data); }
  handleWorldBorderLerpSize(data) { this._worldBorder.handleWorldBorderLerpSize(data); }
  handleWorldBorderWarningDelay(data) { this._worldBorder.handleWorldBorderWarningDelay(data); }
  handleWorldBorderWarningReach(data) { this._worldBorder.handleWorldBorderWarningReach(data); }

  // Tab list
  handlePlayerInfo(data) {
    this.playerInfoPackets.push({
      action: structuredClone(data.action),
      data: structuredClone(data.data || []),
    });

    if (data.data) {
      for (const entry of data.data) {
        const uuid = entry.uuid;
        if (!uuid) continue;
        const existing = this.playerInfo.get(uuid) || {};
        this.playerInfo.set(uuid, { ...existing, ...entry });
        this.removedPlayers.delete(uuid);
      }
    }
  }

  handlePlayerRemove(data) {
    if (data.players) {
      for (const uuid of data.players) {
        this.playerInfo.delete(uuid);
        this.removedPlayers.add(uuid);
      }
    }
  }

  handlePlayerListHeader(data) {
    this.playerListHeader = { ...data };
  }

  // Scoreboard — delegate to ScoreboardCache
  handleScoreboardObjective(data) { this._scoreboard.handleScoreboardObjective(data); }
  handleScoreboardDisplayObjective(data) { this._scoreboard.handleScoreboardDisplayObjective(data); }
  handleScoreboardScore(data) { this._scoreboard.handleScoreboardScore(data); }
  handleResetScore(data) { this._scoreboard.handleResetScore(data); }
  handleTeams(data) { this._scoreboard.handleTeams(data); }

  // Boss bars
  handleBossBar(data) {
    if (data.action === 1) {
      // Remove
      this.bossBars.delete(data.entityUUID);
    } else {
      const existing = this.bossBars.get(data.entityUUID) || {};
      this.bossBars.set(data.entityUUID, { ...existing, ...data });
    }
  }

  handleTags(data) {
    this.tags = { ...data };
  }

  handleServerData(data) {
    this.serverData = { ...data };
  }

  handleSimulationDistance(data) {
    this.simulationDistance = { ...data };
  }

  handleUpdateViewDistance(data) {
    this.viewDistance = { ...data };
  }

  handleDeclareCommands(data) {
    this.declareCommands = { ...data };
  }

  handleUpdateViewPosition(data) {
    this.viewPosition = { ...data };
  }

  /**
   * Get all replay packets for misc state.
   * @returns {Array<{name: string, data: object}>}
   */
  getReplayPackets() {
    const packets = [];

    if (this.tags) {
      packets.push({ name: 'tags', data: this.tags });
    }

    if (this.declareCommands) {
      packets.push({ name: 'declare_commands', data: this.declareCommands });
    }

    if (this.serverData) {
      packets.push({ name: 'server_data', data: this.serverData });
    }

    if (this.time) {
      packets.push({ name: 'update_time', data: this.time });
    }

    // Weather
    if (this.weather.raining) {
      packets.push({ name: 'game_state_change', data: { reason: 1, gameMode: 0 } });
      if (this.weather.rainLevel != null) {
        packets.push({ name: 'game_state_change', data: { reason: 7, gameMode: this.weather.rainLevel } });
      }
      if (this.weather.thunderLevel != null) {
        packets.push({ name: 'game_state_change', data: { reason: 8, gameMode: this.weather.thunderLevel } });
      }
    }

    // World border
    packets.push(...this._worldBorder.getReplayPackets());

    // Simulation distance + view distance
    if (this.simulationDistance) {
      packets.push({ name: 'simulation_distance', data: this.simulationDistance });
    }
    if (this.viewDistance) {
      packets.push({ name: 'update_view_distance', data: this.viewDistance });
    }

    // update_view_position is sent by StateReplayer after chunks + player position

    // player_info is replayed verbatim via getPlayerInfoReplayPackets()

    if (this.playerListHeader) {
      packets.push({ name: 'playerlist_header', data: this.playerListHeader });
    }

    // Scoreboard
    packets.push(...this._scoreboard.getReplayPackets());

    // Boss bars
    for (const [, bar] of this.bossBars) {
      // Send as "add" action
      packets.push({ name: 'boss_bar', data: { ...bar, action: 0 } });
    }

    return packets;
  }

  /**
   * Replay player_info packets exactly as received from the server.
   * @returns {Array<{name: string, data: object}>}
   */
  getPlayerInfoReplayPackets() {
    return this.playerInfoPackets.map((pkt) => ({
      name: 'player_info',
      data: pkt,
    }));
  }

  /**
   * UUIDs that were added via player_info (for filtering live updates).
   */
  getKnownPlayerUuids() {
    const uuids = new Set();
    for (const pkt of this.playerInfoPackets) {
      if (pkt.action?.add_player) {
        for (const entry of pkt.data) {
          if (entry.uuid) uuids.add(entry.uuid);
        }
      }
    }
    for (const [uuid, entry] of this.playerInfo) {
      if (entry.player) uuids.add(uuid);
    }
    return uuids;
  }

  clear() {
    this.time = null;
    this.weather = { raining: false, rainLevel: null, thunderLevel: null };
    this._worldBorder.clear();
    this.playerInfo.clear();
    this.playerInfoPackets = [];
    this.playerListHeader = null;
    this._scoreboard.clear();
    this.bossBars.clear();
    this.tags = null;
    this.serverData = null;
    this.simulationDistance = null;
    this.viewDistance = null;
    this.declareCommands = null;
    this.viewPosition = null;
    this.removedPlayers.clear();
  }
}

module.exports = { MiscCache };

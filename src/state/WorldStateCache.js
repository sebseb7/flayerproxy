const { createLogger } = require('../utils/logger');
const { ChunkCache } = require('./ChunkCache');
const { worldBoundsForDimension, dimensionNameFromLogin } = require('./chunkMerge');
const { EntityCache } = require('./EntityCache');
const { PlayerStateCache } = require('./PlayerStateCache');
const { InventoryCache } = require('./InventoryCache');
const { MiscCache } = require('./MiscCache');
const { JoinSyncCache } = require('./JoinSyncCache');

const log = createLogger('WorldState');

function cloneConfigData(data) {
  if (data == null || typeof data !== 'object') return data;
  const cloned = structuredClone(data);
  // structuredClone turns Buffer into Uint8Array; custom_payload.data must stay Buffer
  if (Buffer.isBuffer(data.data)) {
    cloned.data = Buffer.from(data.data);
  }
  return cloned;
}

/**
 * Master cache coordinator.
 * Routes incoming server packets to the appropriate sub-cache.
 */
class WorldStateCache {
  constructor(config) {
    this._serverVersion = config.server?.version ?? '1.21.10';
    this._defaultViewDistance = config.bot?.viewDistance ?? 10;
    this.chunks = new ChunkCache(config.cache.maxChunks, {
      version: this._serverVersion,
      getWorldBounds: () =>
        worldBoundsForDimension(
          this._serverVersion,
          dimensionNameFromLogin(this.player.loginPacket)
        ),
    });
    this.entities = new EntityCache();
    this.player = new PlayerStateCache();
    this.inventory = new InventoryCache();
    this.misc = new MiscCache();
    this.joinSync = new JoinSyncCache();

    /** Parsed configuration-phase packets (fallback registry codec build) */
    this.configPackets = [];

    /** Upstream config packets in receive order for proxy replay */
    this.configReplay = [];

    /** Set when upstream sends finish_configuration */
    this.configReady = false;

    /** Track whether we've received the login packet */
    this.initialized = false;
  }

  /**
   * Store a configuration-phase packet from the bot upstream connection.
   * @param {string} name
   * @param {object} data
   * @param {Buffer} [buffer]
   */
  handleConfigReplayPacket(name, data, buffer) {
    const cloned = cloneConfigData(data);
    const idx = this.configPackets.findIndex(p => p.name === name);
    if (name === 'registry_data') {
      this.configPackets.push({ name, data: cloned });
    } else if (idx >= 0) {
      this.configPackets[idx] = { name, data: cloned };
    } else {
      this.configPackets.push({ name, data: cloned });
    }

    if (name === 'finish_configuration') {
      this.configReady = true;
      return;
    }

    this.configReplay.push({
      name,
      data: cloned,
      buffer: buffer?.length ? Buffer.from(buffer) : null,
    });
  }

  isConfigReady() {
    return this.configReady;
  }

  hasConfigReplay() {
    return this.configReady && this.configReplay.length > 0;
  }

  getConfigReplayEntries() {
    return this.configReplay;
  }

  /**
   * Build a registryCodec object from captured server registry_data packets.
   * Used by the proxy so clients receive the real server's registries, not minecraft-data defaults.
   * @returns {object|null}
   */
  buildRegistryCodec() {
    const registryPackets = this.configPackets.filter(p => p.name === 'registry_data');
    if (registryPackets.length === 0) return null;

    const first = registryPackets[0].data;
    if (first.codec) {
      return first;
    }

    const codec = {};
    for (const { data } of registryPackets) {
      if (data.id) {
        codec[data.id] = data;
      }
    }
    return Object.keys(codec).length > 0 ? codec : null;
  }

  /** @deprecated use hasConfigReplay */
  hasRawConfigPackets() {
    return this.hasConfigReplay();
  }

  /**
   * Chunk view center + distance for cache retention (matches server ChunkTrackingView).
   * @returns {{ centerChunkX: number, centerChunkZ: number, viewDistance: number }|null}
   */
  _getChunkViewContext() {
    const viewDistance =
      this.misc.viewDistance?.viewDistance ?? this._defaultViewDistance;

    if (this.misc.viewPosition?.chunkX != null && this.misc.viewPosition?.chunkZ != null) {
      return {
        centerChunkX: this.misc.viewPosition.chunkX,
        centerChunkZ: this.misc.viewPosition.chunkZ,
        viewDistance,
      };
    }

    const pos = this.player.position;
    if (pos?.x != null && pos?.z != null) {
      return {
        centerChunkX: Math.floor(pos.x / 16),
        centerChunkZ: Math.floor(pos.z / 16),
        viewDistance,
      };
    }

    return null;
  }

  _forgetChunksOutsideView() {
    const view = this._getChunkViewContext();
    if (!view) return;
    this.chunks.forgetOutsideView(view.centerChunkX, view.centerChunkZ, view.viewDistance);
  }

  /**
   * Process a server->client play packet and route to appropriate cache.
   * @param {string} name - packet name
   * @param {object} data - packet data
   * @param {Buffer} [buffer] - raw packet bytes from the server
   */
  handleServerPacket(name, data, buffer) {
    switch (name) {
      // Player state
      case 'login':
        this.player.handleLogin(data);
        this.initialized = true;
        break;
      case 'position':
        this.player.handlePosition(data);
        this._forgetChunksOutsideView();
        break;
      case 'update_health':
        this.player.handleUpdateHealth(data);
        break;
      case 'experience':
        this.player.handleExperience(data);
        break;
      case 'abilities':
        this.player.handleAbilities(data);
        break;
      case 'entity_status':
        this.player.handleEntityStatus(data);
        break;
      case 'spawn_position':
        this.player.handleSpawnPosition(data);
        break;
      case 'difficulty':
        this.player.handleDifficulty(data);
        break;
      case 'respawn':
        this.player.handleRespawn(data);
        this.entities.clear();
        break;

      // Chunks
      case 'map_chunk':
        this.chunks.handleMapChunk(data, buffer, this._getChunkViewContext() ?? undefined);
        break;
      case 'update_light':
        this.chunks.handleUpdateLight(data);
        break;
      case 'unload_chunk':
        this.chunks.handleUnloadChunk(data);
        break;
      case 'block_change':
        this.chunks.handleBlockChange(data);
        break;
      case 'multi_block_change':
        this.chunks.handleMultiBlockChange(data);
        break;

      // Entities
      case 'spawn_entity':
        this.entities.handleSpawnEntity(data);
        break;
      case 'entity_metadata':
        this.entities.handleEntityMetadata(data);
        break;
      case 'entity_equipment':
        this.entities.handleEntityEquipment(data);
        break;
      case 'entity_effect':
        this.player.handleEntityEffect(data);
        this.entities.handleEntityEffect(data);
        break;
      case 'remove_entity_effect':
        this.player.handleRemoveEntityEffect(data);
        this.entities.handleRemoveEntityEffect(data);
        break;
      case 'entity_destroy':
        this.entities.handleEntityDestroy(data);
        break;
      case 'set_passengers':
        this.entities.handleSetPassengers(data);
        break;
      case 'entity_teleport':
        this.entities.handleEntityTeleport(data);
        break;
      case 'rel_entity_move':
        this.entities.handleRelEntityMove(data);
        break;
      case 'entity_move_look':
        this.entities.handleEntityMoveLook(data);
        break;
      case 'sync_entity_position':
        this.entities.handleSyncEntityPosition(data);
        break;

      // Inventory
      case 'window_items':
        this.inventory.handleWindowItems(data);
        break;
      case 'set_slot':
        this.inventory.handleSetSlot(data);
        break;
      case 'held_item_slot':
        this.inventory.handleHeldItemSlot(data);
        break;
      case 'set_player_inventory':
        this.inventory.handleSetPlayerInventory(data);
        break;
      case 'set_cursor_item':
        this.inventory.handleSetCursorItem(data);
        break;

      // Time & weather
      case 'update_time':
        this.misc.handleUpdateTime(data);
        break;
      case 'game_state_change':
        this.player.handleGameStateChange(data);
        this.misc.handleGameStateChange(data);
        break;

      // World border
      case 'initialize_world_border':
        this.misc.handleInitWorldBorder(data);
        break;
      case 'world_border_center':
        this.misc.handleWorldBorderCenter(data);
        break;
      case 'world_border_size':
        this.misc.handleWorldBorderSize(data);
        break;
      case 'world_border_lerp_size':
        this.misc.handleWorldBorderLerpSize(data);
        break;
      case 'world_border_warning_delay':
        this.misc.handleWorldBorderWarningDelay(data);
        break;
      case 'world_border_warning_reach':
        this.misc.handleWorldBorderWarningReach(data);
        break;

      // Tab list
      case 'player_info':
        this.misc.handlePlayerInfo(data);
        break;
      case 'player_remove':
        this.misc.handlePlayerRemove(data);
        break;
      case 'playerlist_header':
        this.misc.handlePlayerListHeader(data);
        break;

      // Scoreboard
      case 'scoreboard_objective':
        this.misc.handleScoreboardObjective(data);
        break;
      case 'scoreboard_display_objective':
        this.misc.handleScoreboardDisplayObjective(data);
        break;
      case 'scoreboard_score':
        this.misc.handleScoreboardScore(data);
        break;
      case 'reset_score':
        this.misc.handleResetScore(data);
        break;
      case 'teams':
        this.misc.handleTeams(data);
        break;

      // Boss bar
      case 'boss_bar':
        this.misc.handleBossBar(data);
        break;

      case 'tracked_waypoint':
        this.misc.handleTrackedWaypoint(data);
        break;

      // Tags
      case 'tags':
        this.misc.handleTags(data);
        break;

      // Server data
      case 'server_data':
        this.misc.handleServerData(data);
        break;

      // View distance / simulation
      case 'simulation_distance':
        this.misc.handleSimulationDistance(data);
        break;
      case 'update_view_distance':
        this.misc.handleUpdateViewDistance(data);
        this._forgetChunksOutsideView();
        break;
      case 'declare_commands':
        this.misc.handleDeclareCommands(data);
        break;
      case 'update_view_position':
        this.misc.handleUpdateViewPosition(data);
        this._forgetChunksOutsideView();
        break;

      case 'update_recipes':
      case 'declare_recipes':
      case 'advancements':
      case 'recipe_book_add':
      case 'recipe_book_settings':
        this.joinSync.handlePacket(name, data);
        break;

      // Packets we intentionally don't cache (ephemeral):
      // sound_effect, entity_sound_effect, world_particles, animation,
      // block_break_animation, explosion, world_event, player_chat,
      // system_chat, etc.
      default:
        break;
    }
  }

  /**
   * Get a summary of cached state for logging.
   */
  getSummary() {
    return {
      chunks: this.chunks.size,
      entities: this.entities.size,
      initialized: this.initialized,
      hasPosition: !!this.player.position,
      hasInventory: !!this.inventory.windowItems,
      playerInfoEntries: this.misc.playerInfo.size,
    };
  }

  clear() {
    this.chunks.clear();
    this.entities.clear();
    this.player.clear();
    this.inventory.clear();
    this.misc.clear();
    this.joinSync.clear();
    this.configPackets = [];
    this.configReplay = [];
    this.configReady = false;
    this.initialized = false;
  }
}

module.exports = { WorldStateCache };

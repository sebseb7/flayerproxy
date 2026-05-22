const { createLogger } = require('../utils/logger');
const log = createLogger('JoinSyncCache');

/**
 * Caches play packets from PlayerList.placeNewPlayer / PlayerAdvancements
 * that the proxy client would otherwise never see (bot got them at login).
 */
class JoinSyncCache {
  constructor() {
    /** @type {{ name: string, data: object }|null} */
    this.updateRecipes = null;
    /** @type {Array<{ name: string, data: object }>} */
    this.advancementPackets = [];
    /** @type {Array<{ name: string, data: object }>} */
    this.recipeBookAdd = [];
    this.recipeBookSettings = null;
  }

  handlePacket(name, data) {
    switch (name) {
      case 'update_recipes':
      case 'declare_recipes':
        this.updateRecipes = { name, data: structuredClone(data) };
        log.debug(`Cached ${name}`);
        break;

      case 'advancements':
        if (data.reset) {
          this.advancementPackets = [{ name, data: structuredClone(data) }];
          log.info('Cached full advancements snapshot (reset)');
        } else {
          this.advancementPackets.push({ name, data: structuredClone(data) });
        }
        break;

      case 'recipe_book_add':
        if (data.replace) {
          this.recipeBookAdd = [{ name, data: structuredClone(data) }];
        } else {
          this.recipeBookAdd.push({ name, data: structuredClone(data) });
        }
        break;

      case 'recipe_book_settings':
        this.recipeBookSettings = structuredClone(data);
        break;

      default:
        break;
    }
  }

  /**
   * Packets to send after inventory, before teleport (placeNewPlayer order).
   * @returns {Array<{ name: string, data: object }>}
   */
  getReplayPackets() {
    const packets = [];
    if (this.updateRecipes) packets.push(this.updateRecipes);
    if (this.recipeBookSettings) {
      packets.push({ name: 'recipe_book_settings', data: this.recipeBookSettings });
    }
    for (const pkt of this.recipeBookAdd) {
      packets.push(pkt);
    }
    for (const pkt of this.advancementPackets) {
      packets.push(pkt);
    }
    return packets;
  }

  clear() {
    this.updateRecipes = null;
    this.advancementPackets = [];
    this.recipeBookAdd = [];
    this.recipeBookSettings = null;
  }
}

module.exports = { JoinSyncCache };

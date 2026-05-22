const { createLogger } = require('../utils/logger');
const log = createLogger('InventoryCache');

/**
 * Caches inventory state: window items, individual slot updates, held item.
 */
class InventoryCache {
  constructor() {
    /** Full window_items packet data (usually inventory slot 0) */
    this.windowItems = null;
    /** Individual set_slot updates (keyed by "windowId:slot") */
    this.slotUpdates = new Map();
    /** Currently held item slot index */
    this.heldItemSlot = null;
    /** set_player_inventory packet data */
    this.playerInventory = null;
    /** set_cursor_item packet data */
    this.cursorItem = null;
  }

  handleWindowItems(data) {
    this.windowItems = { ...data };
    // Clear individual slot updates since we have a full snapshot
    this.slotUpdates.clear();
  }

  handleSetSlot(data) {
    const key = `${data.windowId}:${data.slot}`;
    this.slotUpdates.set(key, { ...data });
  }

  handleHeldItemSlot(data) {
    this.heldItemSlot = { ...data };
  }

  handleSetPlayerInventory(data) {
    this.playerInventory = { ...data };
  }

  handleSetCursorItem(data) {
    this.cursorItem = { ...data };
  }

  /**
   * Get the packets needed to replay inventory state.
   * @returns {Array<{name: string, data: object}>}
   */
  getReplayPackets() {
    const packets = [];

    if (this.windowItems) {
      packets.push({ name: 'window_items', data: this.windowItems });
    }

    // Apply any set_slot updates that came after window_items
    for (const [, slotData] of this.slotUpdates) {
      packets.push({ name: 'set_slot', data: slotData });
    }

    if (this.heldItemSlot) {
      packets.push({ name: 'held_item_slot', data: this.heldItemSlot });
    }

    if (this.playerInventory) {
      packets.push({ name: 'set_player_inventory', data: this.playerInventory });
    }

    if (this.cursorItem) {
      packets.push({ name: 'set_cursor_item', data: this.cursorItem });
    }

    return packets;
  }

  clear() {
    this.windowItems = null;
    this.slotUpdates.clear();
    this.heldItemSlot = null;
    this.playerInventory = null;
    this.cursorItem = null;
  }
}

module.exports = { InventoryCache };

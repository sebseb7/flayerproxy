/**
 * Caches world border state: initialize, center, size, lerp, and warnings.
 */
class WorldBorderCache {
  constructor() {
    this.initBorder = null;     // initialize_world_border
    this.center = null;         // world_border_center
    this.size = null;           // world_border_size
    this.lerpSize = null;       // world_border_lerp_size
    this.warningDelay = null;
    this.warningReach = null;
  }

  handleInitWorldBorder(data) {
    this.initBorder = { ...data };
  }

  handleWorldBorderCenter(data) {
    this.center = { ...data };
  }

  handleWorldBorderSize(data) {
    this.size = { ...data };
  }

  handleWorldBorderLerpSize(data) {
    this.lerpSize = { ...data };
  }

  handleWorldBorderWarningDelay(data) {
    this.warningDelay = { ...data };
  }

  handleWorldBorderWarningReach(data) {
    this.warningReach = { ...data };
  }

  /**
   * @returns {Array<{name: string, data: object}>}
   */
  getReplayPackets() {
    const packets = [];
    if (this.initBorder) {
      packets.push({ name: 'initialize_world_border', data: this.initBorder });
    }
    if (this.center) {
      packets.push({ name: 'world_border_center', data: this.center });
    }
    if (this.size) {
      packets.push({ name: 'world_border_size', data: this.size });
    }
    return packets;
  }

  clear() {
    this.initBorder = null;
    this.center = null;
    this.size = null;
    this.lerpSize = null;
    this.warningDelay = null;
    this.warningReach = null;
  }
}

module.exports = { WorldBorderCache };

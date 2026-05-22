const { createLogger } = require('../utils/logger');

const log = createLogger('TerrainCapture');

/** S2C packets to record for handoff terrain (decoded params, re-encoded on replay). */
const TERRAIN_PACKET_NAMES = new Set([
  'game_state_change',
  'update_view_position',
  'update_view_distance',
  'chunk_batch_start',
  'map_chunk',
  'update_light',
  'chunk_batch_finished',
]);

/**
 * Records a slice of upstream S2C play packets (usually one chunk batch) for replay.
 */
class TerrainCapture {
  constructor() {
    this.active = false;
    /** @type {{ name: string, data: object }[]} */
    this.packets = [];
  }

  start() {
    this.active = true;
    this.packets = [];
    log.debug('Terrain capture started');
  }

  /**
   * @param {string} name
   * @param {object} data
   */
  push(name, data, _buffer) {
    if (!this.active || !TERRAIN_PACKET_NAMES.has(name)) return;

    this.packets.push({
      name,
      data: structuredClone(data),
    });

    if (name === 'chunk_batch_finished') {
      this.active = false;
      log.info(`Terrain capture complete (${this.packets.length} packets, batchSize=${data.batchSize})`);
    }
  }

  stop() {
    if (this.active) {
      const summary = this.summarize();
      log.info(
        `Terrain capture stopped early (${this.packets.length} packets: ${summary})`,
      );
    }
    this.active = false;
  }

  summarize() {
    const counts = {};
    for (const p of this.packets) {
      counts[p.name] = (counts[p.name] ?? 0) + 1;
    }
    return Object.entries(counts)
      .map(([k, n]) => `${k}:${n}`)
      .join(', ');
  }

  hasBatch() {
    return this.packets.some((p) => p.name === 'chunk_batch_finished');
  }

  getPackets() {
    return this.packets;
  }

  clear() {
    this.active = false;
    this.packets = [];
  }
}

module.exports = { TerrainCapture, TERRAIN_PACKET_NAMES };

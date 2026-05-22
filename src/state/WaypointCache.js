const { waypointKey, normalizeWaypointOperation } = require('../utils/waypointRelay');

/**
 * Caches active Journeys / locator waypoints (tracked_waypoint) for handoff replay.
 */
class WaypointCache {
  constructor() {
    /** @type {Map<string, object>} */
    this.waypoints = new Map();
  }

  handleTrackedWaypoint(data) {
    const op = normalizeWaypointOperation(data?.operation);
    const key = waypointKey(data?.waypoint);
    if (!op || !key) return;

    if (op === 'untrack') {
      this.waypoints.delete(key);
      return;
    }

    const incoming = data.waypoint;
    if (op === 'track') {
      this.waypoints.set(key, cloneWaypoint(incoming));
      return;
    }

    if (op === 'update') {
      const existing = this.waypoints.get(key);
      this.waypoints.set(key, existing ? mergeWaypoint(existing, incoming) : cloneWaypoint(incoming));
    }
  }

  getKnownKeys() {
    return [...this.waypoints.keys()];
  }

  getReplayPackets() {
    const packets = [];
    for (const waypoint of this.waypoints.values()) {
      packets.push({
        name: 'tracked_waypoint',
        data: { operation: 0, waypoint: cloneWaypoint(waypoint) },
      });
    }
    return packets;
  }

  clear() {
    this.waypoints.clear();
  }
}

function cloneWaypoint(waypoint) {
  return JSON.parse(JSON.stringify(waypoint));
}

function mergeWaypoint(existing, incoming) {
  const merged = { ...existing, ...incoming };
  if (incoming.icon) {
    merged.icon = { ...existing.icon, ...incoming.icon };
  }
  return merged;
}

module.exports = { WaypointCache };

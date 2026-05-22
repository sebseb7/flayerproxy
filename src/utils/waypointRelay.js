/**
 * ClientboundTrackedWaypointPacket is session-ordered (track → update).
 * Proxy clients that join mid-session must not see orphan updates.
 */

function waypointKey(waypoint) {
  if (!waypoint) return null;
  if (waypoint.uuid != null) {
    return typeof waypoint.uuid === 'string' ? waypoint.uuid : String(waypoint.uuid);
  }
  if (waypoint.id != null) return `id:${waypoint.id}`;
  return null;
}

function normalizeWaypointOperation(operation) {
  if (operation === 0 || operation === 'track') return 'track';
  if (operation === 1 || operation === 'untrack') return 'untrack';
  if (operation === 2 || operation === 'update') return 'update';
  return null;
}

/**
 * @param {object} data - tracked_waypoint packet body
 * @param {Set<string>} knownKeys - keys the client has seen a track for
 * @returns {boolean} whether to forward to the client
 */
function shouldForwardWaypointToClient(data, knownKeys) {
  const op = normalizeWaypointOperation(data?.operation);
  const key = waypointKey(data?.waypoint);
  if (!op || !key) return false;

  if (op === 'track') {
    knownKeys.add(key);
    return true;
  }
  if (op === 'untrack') {
    knownKeys.delete(key);
    return true;
  }
  if (op === 'update') {
    return knownKeys.has(key);
  }
  return false;
}

module.exports = {
  waypointKey,
  normalizeWaypointOperation,
  shouldForwardWaypointToClient,
};

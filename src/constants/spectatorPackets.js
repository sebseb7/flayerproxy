/** Gamemode id for spectator (clientbound game_state_change). */
const SPECTATOR_GAMEMODE = 3;

/**
 * Lower feet Y on clientbound position for spectators while the bot sneaks.
 * Approximates standing vs crouching eye height (~1.62 vs ~1.27 blocks above feet).
 */
const SPECTATOR_SNEAK_EYE_Y_OFFSET = 0.35;

/** ClientboundAnimatePacket (minecraft-data: animation). */
const ANIMATION_SWING_MAIN_HAND = 0;
const ANIMATION_SWING_OFF_HAND = 3;

/** C2S packets allowed on the spectator port (everything else is dropped). */
const SPECTATOR_ALLOWED_C2S = new Set([
  'chunk_batch_received',
  'teleport_confirm',
  'keep_alive',
  'message_acknowledgement',
  'ping_request',
]);

/**
 * S2C packets dropped on spectator fan-out (not session-ordered waypoints — those use waypointRelay).
 */
const SPECTATOR_BLOCKED_S2C = new Set(['cookie_request', 'store_cookie']);

/** @deprecated unused; waypoints use cache replay + shouldForwardWaypointToClient */
const SESSION_ORDERED_BLOCKED_S2C = SPECTATOR_BLOCKED_S2C;

/** Movement-ish C2S — trigger camera lock + position snap when received. */
const SPECTATOR_MOVEMENT_C2S = new Set([
  'position',
  'position_look',
  'look',
  'flying',
  'vehicle_move',
  'steer_vehicle',
  'steer_boat',
  'paddle_boat',
  'player_input',
  'entity_action',
  'abilities',
  'player_abilities',
]);

module.exports = {
  SPECTATOR_GAMEMODE,
  SPECTATOR_SNEAK_EYE_Y_OFFSET,
  SPECTATOR_ALLOWED_C2S,
  SESSION_ORDERED_BLOCKED_S2C,
  SPECTATOR_BLOCKED_S2C,
  SPECTATOR_MOVEMENT_C2S,
  ANIMATION_SWING_MAIN_HAND,
  ANIMATION_SWING_OFF_HAND,
};

/** Gamemode id for spectator (clientbound game_state_change). */
const SPECTATOR_GAMEMODE = 3;

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
 * S2C packets not forwarded to spectators.
 * tracked_waypoint (journeys/locator bar) is session-ordered (track → update);
 * mid-join spectators only see updates and disconnect.
 */
const SPECTATOR_BLOCKED_S2C = new Set(['tracked_waypoint']);

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
  SPECTATOR_ALLOWED_C2S,
  SPECTATOR_BLOCKED_S2C,
  SPECTATOR_MOVEMENT_C2S,
  ANIMATION_SWING_MAIN_HAND,
  ANIMATION_SWING_OFF_HAND,
};

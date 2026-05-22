const {
  ANIMATION_SWING_MAIN_HAND,
  ANIMATION_SWING_OFF_HAND,
  SPECTATOR_SNEAK_EYE_Y_OFFSET,
} = require('../constants/spectatorPackets');

const KEY_SHARED_FLAGS = 0;
const KEY_POSE = 6;
const TYPE_SHARED_FLAGS = 'byte';
const TYPE_POSE = 'pose';

const POSE_STANDING = 0;
const POSE_CROUCHING = 5;
const SHARED_FLAGS_CROUCH_BIT = 0x02;

const INPUT_SHIFT_BIT = 0x20;
const INPUT_JUMP_BIT = 0x10;

/** Live bot entity id first — login cache can be stale after respawn. */
function getPlayerEntityId(worldState, serverConn) {
  return serverConn.bot?.entity?.id ?? worldState.player.entityId ?? null;
}

/**
 * @param {object} data - serverbound player_input
 */
function parsePlayerInput(data) {
  const inputs = data?.inputs;
  if (inputs == null) {
    return { sneaking: false, jumping: false };
  }
  if (typeof inputs === 'number') {
    return {
      sneaking: (inputs & INPUT_SHIFT_BIT) !== 0,
      jumping: (inputs & INPUT_JUMP_BIT) !== 0,
    };
  }
  const raw = inputs._value ?? inputs.value;
  if (typeof raw === 'number') {
    return {
      sneaking: (raw & INPUT_SHIFT_BIT) !== 0,
      jumping: (raw & INPUT_JUMP_BIT) !== 0,
    };
  }
  return {
    sneaking: Boolean(inputs.shift),
    jumping: Boolean(inputs.jump),
  };
}

/**
 * @returns {{ entityId: number, metadata: object[] }|null}
 */
function buildPlayerPoseMetadata(worldState, serverConn, sneaking) {
  const entityId = getPlayerEntityId(worldState, serverConn);
  if (entityId == null) return null;

  let sharedFlags = 0;
  let pose = POSE_STANDING;

  const cached =
    worldState.player.entityMetadata ?? worldState.entities.entities.get(entityId)?.metadata;
  const entries = cached?.metadata;
  if (entries?.length) {
    for (const entry of entries) {
      if (entry.key === KEY_SHARED_FLAGS) sharedFlags = Number(entry.value) || 0;
      if (entry.key === KEY_POSE) pose = Number(entry.value) || 0;
    }
  }

  if (sneaking) {
    sharedFlags |= SHARED_FLAGS_CROUCH_BIT;
    pose = POSE_CROUCHING;
  } else {
    sharedFlags &= ~SHARED_FLAGS_CROUCH_BIT;
    if (pose === POSE_CROUCHING) pose = POSE_STANDING;
  }

  return {
    entityId,
    metadata: [
      { key: KEY_SHARED_FLAGS, type: TYPE_SHARED_FLAGS, value: sharedFlags },
      { key: KEY_POSE, type: TYPE_POSE, value: pose },
    ],
  };
}

function buildSwingAnimation(worldState, serverConn, data) {
  const entityId = getPlayerEntityId(worldState, serverConn);
  if (entityId == null) return null;
  const hand = data?.hand ?? 0;
  return {
    entityId,
    animation: hand === 1 ? ANIMATION_SWING_OFF_HAND : ANIMATION_SWING_MAIN_HAND,
  };
}

function buildJumpVelocity(worldState, serverConn) {
  const entityId = getPlayerEntityId(worldState, serverConn);
  const vel = serverConn.bot?.entity?.velocity;
  if (entityId == null) return null;
  return {
    entityId,
    velocity: {
      x: vel?.x ?? 0,
      y: Math.max(vel?.y ?? 0, 0.42),
      z: vel?.z ?? 0,
    },
  };
}

/**
 * Spectators replay the bot login + camera on the same entity id, so pose metadata
 * often does not move the view. Shift feet Y while sneaking to lower effective eye height.
 * @param {import('../session/ServerConnection').ServerConnection} serverConn
 * @param {string} packetName
 * @param {object} data
 */
function applySpectatorSneakYOffset(serverConn, packetName, data) {
  if (!serverConn.botSneaking || data == null) return data;

  const watchId = getPlayerEntityId(serverConn.worldState, serverConn);
  if (packetName === 'sync_entity_position' && watchId != null && data.entityId !== watchId) {
    return data;
  }
  if (packetName !== 'position' && packetName !== 'sync_entity_position') {
    return data;
  }
  if (data.y == null) return data;

  return { ...data, y: data.y - SPECTATOR_SNEAK_EYE_Y_OFFSET };
}

/**
 * Relay play-client actions that the server does not echo on the bot connection.
 * @param {import('../session/ServerConnection').ServerConnection} serverConn
 * @param {string} packetName
 * @param {object} data
 * @param {{ lastSneak: boolean|null, lastJump: boolean }} relayState
 */
function relayPlayClientVisual(serverConn, packetName, data, relayState) {
  if (packetName === 'arm_animation') {
    const anim = buildSwingAnimation(serverConn.worldState, serverConn, data);
    if (anim) serverConn.emitBotVisual('animation', anim);
    return;
  }

  if (packetName === 'player_input') {
    const { sneaking, jumping } = parsePlayerInput(data);
    if (relayState.lastSneak !== sneaking) {
      relayState.lastSneak = sneaking;
      serverConn.emitPlayerPoseVisual(sneaking);
    }
    if (jumping && !relayState.lastJump) {
      const vel = buildJumpVelocity(serverConn.worldState, serverConn);
      if (vel) serverConn.emitBotVisual('entity_velocity', vel);
    }
    relayState.lastJump = jumping;
  }
}

module.exports = {
  getPlayerEntityId,
  parsePlayerInput,
  buildPlayerPoseMetadata,
  applySpectatorSneakYOffset,
  relayPlayClientVisual,
};

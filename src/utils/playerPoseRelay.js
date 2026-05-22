/** Player entity metadata keys (minecraft-data 1.21.x). */
const KEY_SHARED_FLAGS = 0;
const KEY_POSE = 6;
/** Must be string mapper names — numeric type ids encode broken set_entity_data packets. */
const TYPE_SHARED_FLAGS = 'byte';
const TYPE_POSE = 'pose';

const POSE_STANDING = 0;
const POSE_CROUCHING = 5;
const SHARED_FLAGS_CROUCH_BIT = 0x02;
const INPUT_SHIFT_BIT = 0x20;

function getPlayerEntityId(worldState, serverConn) {
  return worldState.player.entityId ?? serverConn.bot?.entity?.id ?? null;
}

/**
 * @param {object} data - serverbound player_input
 * @returns {boolean}
 */
function isSneakingFromPlayerInput(data) {
  const inputs = data?.inputs;
  if (inputs == null) return false;
  if (typeof inputs === 'number') return (inputs & INPUT_SHIFT_BIT) !== 0;
  if (typeof inputs === 'object') return Boolean(inputs.shift);
  return false;
}

/**
 * Build clientbound entity_metadata for crouch/stand (server does not echo to self).
 * @returns {{ entityId: number, metadata: object[] }|null}
 */
function buildPlayerPoseMetadata(worldState, serverConn, sneaking) {
  const entityId = getPlayerEntityId(worldState, serverConn);
  if (entityId == null) return null;

  let sharedFlags = 0;
  let pose = POSE_STANDING;

  const cached = worldState.entities.entities.get(entityId)?.metadata;
  if (cached?.metadata?.length) {
    for (const entry of cached.metadata) {
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

module.exports = {
  isSneakingFromPlayerInput,
  buildPlayerPoseMetadata,
};

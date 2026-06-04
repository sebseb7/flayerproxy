#include "internal.h"
/* Good for: Parse on-ground and horizontal-collision flags from C2S move.
 * Callers: c2s_move.c (same file).
 */

static lc_status read_c2s_move_flags(lc_buf *b, lc_c2s_move_flags *out) {
  uint8_t raw;
  if (lc_buf_read_u8(b, &raw) != LC_OK) return LC_ERR_TRUNCATED;
  out->raw = raw;
  out->on_ground = (raw & 1) ? 1 : 0;
  out->horizontal_collision = (raw & 2) ? 1 : 0;
  return LC_OK;
}
/* Good for: Decode Minecraft wire payload for c2s position into a struct.
 * Callers: chunk_stream_receiver.c, decode_wire.c, mc_c2s_log.c, mc_static_server.c.
 */

lc_status lc_parse_c2s_position(const uint8_t *data, size_t len, lc_c2s_position *out) {
  lc_buf b;
  lc_buf_init(&b, data, len);
  if (lc_buf_read_f64_be(&b, &out->x) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_f64_be(&b, &out->y) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_f64_be(&b, &out->z) != LC_OK) return LC_ERR_TRUNCATED;
  if (read_c2s_move_flags(&b, &out->flags) != LC_OK) return LC_ERR_TRUNCATED;
  return LC_OK;
}
/* Good for: Decode Minecraft wire payload for c2s position look into a struct.
 * Callers: chunk_stream_receiver.c, decode_wire.c, mc_c2s_log.c, mc_static_server.c.
 */

lc_status lc_parse_c2s_position_look(const uint8_t *data, size_t len, lc_c2s_position_look *out) {
  lc_buf b;
  lc_buf_init(&b, data, len);
  if (lc_buf_read_f64_be(&b, &out->x) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_f64_be(&b, &out->y) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_f64_be(&b, &out->z) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_f32_be(&b, &out->yaw) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_f32_be(&b, &out->pitch) != LC_OK) return LC_ERR_TRUNCATED;
  if (read_c2s_move_flags(&b, &out->flags) != LC_OK) return LC_ERR_TRUNCATED;
  return LC_OK;
}
/* Good for: Decode Minecraft wire payload for c2s look into a struct.
 * Callers: chunk_stream_receiver.c, decode_wire.c, mc_c2s_log.c.
 */

lc_status lc_parse_c2s_look(const uint8_t *data, size_t len, lc_c2s_look *out) {
  lc_buf b;
  lc_buf_init(&b, data, len);
  if (lc_buf_read_f32_be(&b, &out->yaw) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_f32_be(&b, &out->pitch) != LC_OK) return LC_ERR_TRUNCATED;
  if (read_c2s_move_flags(&b, &out->flags) != LC_OK) return LC_ERR_TRUNCATED;
  return LC_OK;
}
/* Good for: Decode Minecraft wire payload for c2s flying into a struct.
 * Callers: chunk_stream_receiver.c, decode_wire.c, mc_c2s_log.c.
 */

lc_status lc_parse_c2s_flying(const uint8_t *data, size_t len, lc_c2s_flying *out) {
  lc_buf b;
  lc_buf_init(&b, data, len);
  if (read_c2s_move_flags(&b, &out->flags) != LC_OK) return LC_ERR_TRUNCATED;
  return LC_OK;
}
/* Good for: Decode Minecraft wire payload for c2s teleport confirm into a struct.
 * Callers: chunk_stream_receiver.c, decode_wire.c, mc_c2s_log.c, mc_static_server.c.
 */

lc_status lc_parse_c2s_teleport_confirm(const uint8_t *data, size_t len, lc_c2s_teleport_confirm *out) {
  lc_buf b;
  lc_buf_init(&b, data, len);
  if (lc_buf_read_varint(&b, &out->teleport_id) != LC_OK) return LC_ERR_TRUNCATED;
  return LC_OK;
}

lc_status lc_parse_c2s_block_dig(const uint8_t *data, size_t len, lc_c2s_block_dig *out) {
  lc_buf b;
  lc_buf_init(&b, data, len);
  if (lc_buf_read_varint(&b, &out->status) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_position(&b, &out->location) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_i8(&b, &out->face) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_varint(&b, &out->sequence) != LC_OK) return LC_ERR_TRUNCATED;
  return LC_OK;
}

lc_status lc_parse_c2s_player_input(const uint8_t *data, size_t len, lc_c2s_player_input *out) {
  lc_buf b;
  lc_buf_init(&b, data, len);
  if (lc_buf_read_u8(&b, &out->raw) != LC_OK) return LC_ERR_TRUNCATED;
  out->forward = (out->raw & 1) ? 1 : 0;
  out->backward = (out->raw & 2) ? 1 : 0;
  out->left = (out->raw & 4) ? 1 : 0;
  out->right = (out->raw & 8) ? 1 : 0;
  out->jump = (out->raw & 16) ? 1 : 0;
  out->shift = (out->raw & 32) ? 1 : 0;
  out->sprint = (out->raw & 64) ? 1 : 0;
  return LC_OK;
}

lc_status lc_parse_c2s_arm_animation(const uint8_t *data, size_t len, lc_c2s_arm_animation *out) {
  lc_buf b;
  lc_buf_init(&b, data, len);
  if (lc_buf_read_varint(&b, &out->hand) != LC_OK) return LC_ERR_TRUNCATED;
  return LC_OK;
}
/* Good for: One-line debug summary of lc_c2s position (sniffer / decode tools).
 * Callers: decode_wire.c, mc_c2s_log.c.
 */

int lc_c2s_position_to_string(const lc_c2s_position *p, char *buf, size_t buflen) {
  if (!p || !buf || buflen == 0) return 0;
  return lc_snprintf(buf, buflen, "c2s_position{pos=(%.3f,%.3f,%.3f),onGround=%d,hColl=%d}",
                     p->x, p->y, p->z, p->flags.on_ground ? 1 : 0,
                     p->flags.horizontal_collision ? 1 : 0);
}
/* Good for: One-line debug summary of lc_c2s position look (sniffer / decode tools).
 * Callers: decode_wire.c, mc_c2s_log.c.
 */

int lc_c2s_position_look_to_string(const lc_c2s_position_look *p, char *buf, size_t buflen) {
  if (!p || !buf || buflen == 0) return 0;
  return lc_snprintf(buf, buflen,
                     "c2s_position_look{pos=(%.3f,%.3f,%.3f),rot=(%.2f,%.2f),onGround=%d,hColl=%d}",
                     p->x, p->y, p->z, p->yaw, p->pitch, p->flags.on_ground ? 1 : 0,
                     p->flags.horizontal_collision ? 1 : 0);
}
/* Good for: One-line debug summary of lc_c2s look (sniffer / decode tools).
 * Callers: decode_wire.c, mc_c2s_log.c.
 */

int lc_c2s_look_to_string(const lc_c2s_look *p, char *buf, size_t buflen) {
  if (!p || !buf || buflen == 0) return 0;
  return lc_snprintf(buf, buflen, "c2s_look{rot=(%.2f,%.2f),onGround=%d,hColl=%d}", p->yaw, p->pitch,
                     p->flags.on_ground ? 1 : 0, p->flags.horizontal_collision ? 1 : 0);
}
/* Good for: One-line debug summary of lc_c2s flying (sniffer / decode tools).
 * Callers: decode_wire.c, mc_c2s_log.c.
 */

int lc_c2s_flying_to_string(const lc_c2s_flying *p, char *buf, size_t buflen) {
  if (!p || !buf || buflen == 0) return 0;
  return lc_snprintf(buf, buflen, "c2s_flying{onGround=%d,hColl=%d}", p->flags.on_ground ? 1 : 0,
                     p->flags.horizontal_collision ? 1 : 0);
}
/* Good for: One-line debug summary of lc_c2s teleport confirm (sniffer / decode tools).
 * Callers: decode_wire.c, mc_c2s_log.c.
 */

int lc_c2s_teleport_confirm_to_string(const lc_c2s_teleport_confirm *p, char *buf, size_t buflen) {
  if (!p || !buf || buflen == 0) return 0;
  return lc_snprintf(buf, buflen, "c2s_teleport_confirm{id=%d}", p->teleport_id);
}

static const char *c2s_block_dig_status_name(int32_t status) {
  static const char *names[] = {
      "start_destroy_block",
      "abort_destroy_block",
      "stop_destroy_block",
      "drop_all_items",
      "drop_item",
      "release_use_item",
      "swap_item_with_offhand",
  };
  if (status < 0 || (size_t)status >= sizeof names / sizeof names[0]) return "?";
  return names[status];
}

static const char *c2s_direction_name(int8_t face) {
  static const char *names[] = {"down", "up", "north", "south", "west", "east"};
  if (face < 0 || (size_t)face >= sizeof names / sizeof names[0]) return "?";
  return names[face];
}

static const char *c2s_hand_name(int32_t hand) {
  return hand == 0 ? "main_hand" : hand == 1 ? "off_hand" : "?";
}

int lc_c2s_block_dig_to_string(const lc_c2s_block_dig *p, char *buf, size_t buflen) {
  if (!p || !buf || buflen == 0) return 0;
  return lc_snprintf(buf, buflen,
                     "c2s_block_dig{status=%s,loc=(%d,%d,%d),face=%s,seq=%d}",
                     c2s_block_dig_status_name(p->status), p->location.x, p->location.y,
                     p->location.z, c2s_direction_name(p->face), p->sequence);
}

int lc_c2s_player_input_to_string(const lc_c2s_player_input *p, char *buf, size_t buflen) {
  if (!p || !buf || buflen == 0) return 0;
  return lc_snprintf(buf, buflen,
                     "c2s_player_input{fwd=%d,back=%d,left=%d,right=%d,jump=%d,shift=%d,sprint=%d,raw=0x%02x}",
                     p->forward, p->backward, p->left, p->right, p->jump, p->shift, p->sprint,
                     p->raw);
}

int lc_c2s_arm_animation_to_string(const lc_c2s_arm_animation *p, char *buf, size_t buflen) {
  if (!p || !buf || buflen == 0) return 0;
  return lc_snprintf(buf, buflen, "c2s_arm_animation{hand=%s}", c2s_hand_name(p->hand));
}

/* Good for: Decode Minecraft wire payload for c2s interact into a struct.
 * Callers: decode_wire.c.
 */

lc_status lc_parse_c2s_interact(const uint8_t *data, size_t len, lc_c2s_interact *out) {
  lc_buf b;
  lc_buf_init(&b, data, len);
  if (lc_buf_read_varint(&b, &out->entity_id) != LC_OK) return LC_ERR_TRUNCATED;
  int32_t action;
  if (lc_buf_read_varint(&b, &action) != LC_OK) return LC_ERR_TRUNCATED;
  out->action = (lc_c2s_interact_action)action;
  out->hand = -1;
  out->at_x = out->at_y = out->at_z = 0.0f;
  if (out->action == LC_INTERACT_ACTION_INTERACT) {
    if (lc_buf_read_varint(&b, &out->hand) != LC_OK) return LC_ERR_TRUNCATED;
  } else if (out->action == LC_INTERACT_ACTION_INTERACT_AT) {
    if (lc_buf_read_f32_be(&b, &out->at_x) != LC_OK) return LC_ERR_TRUNCATED;
    if (lc_buf_read_f32_be(&b, &out->at_y) != LC_OK) return LC_ERR_TRUNCATED;
    if (lc_buf_read_f32_be(&b, &out->at_z) != LC_OK) return LC_ERR_TRUNCATED;
    if (lc_buf_read_varint(&b, &out->hand) != LC_OK) return LC_ERR_TRUNCATED;
  }
  if (lc_buf_read_bool(&b, &out->using_secondary_action) != LC_OK) return LC_ERR_TRUNCATED;
  return LC_OK;
}

int lc_c2s_interact_to_string(const lc_c2s_interact *p, char *buf, size_t buflen) {
  if (!p || !buf || buflen == 0) return 0;
  if (p->action == LC_INTERACT_ACTION_ATTACK) {
    return lc_snprintf(buf, buflen, "c2s_interact{entityId=%d,action=attack,secondary=%d}",
                       p->entity_id, p->using_secondary_action);
  } else if (p->action == LC_INTERACT_ACTION_INTERACT) {
    return lc_snprintf(buf, buflen, "c2s_interact{entityId=%d,action=interact,hand=%s,secondary=%d}",
                       p->entity_id, c2s_hand_name(p->hand), p->using_secondary_action);
  } else if (p->action == LC_INTERACT_ACTION_INTERACT_AT) {
    return lc_snprintf(buf, buflen,
                       "c2s_interact{entityId=%d,action=interact_at,at=(%.2f,%.2f,%.2f),hand=%s,secondary=%d}",
                       p->entity_id, p->at_x, p->at_y, p->at_z, c2s_hand_name(p->hand),
                       p->using_secondary_action);
  }
  return lc_snprintf(buf, buflen, "c2s_interact{entityId=%d,action=%d,secondary=%d}",
                     p->entity_id, (int)p->action, p->using_secondary_action);
}

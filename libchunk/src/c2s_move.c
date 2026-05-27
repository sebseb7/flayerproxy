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
  if (lc_buf_read_f64_le(&b, &out->x) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_f64_le(&b, &out->y) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_f64_le(&b, &out->z) != LC_OK) return LC_ERR_TRUNCATED;
  if (read_c2s_move_flags(&b, &out->flags) != LC_OK) return LC_ERR_TRUNCATED;
  return LC_OK;
}
/* Good for: Decode Minecraft wire payload for c2s position look into a struct.
 * Callers: chunk_stream_receiver.c, decode_wire.c, mc_c2s_log.c, mc_static_server.c.
 */

lc_status lc_parse_c2s_position_look(const uint8_t *data, size_t len, lc_c2s_position_look *out) {
  lc_buf b;
  lc_buf_init(&b, data, len);
  if (lc_buf_read_f64_le(&b, &out->x) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_f64_le(&b, &out->y) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_f64_le(&b, &out->z) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_f32_le(&b, &out->yaw) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_f32_le(&b, &out->pitch) != LC_OK) return LC_ERR_TRUNCATED;
  if (read_c2s_move_flags(&b, &out->flags) != LC_OK) return LC_ERR_TRUNCATED;
  return LC_OK;
}
/* Good for: Decode Minecraft wire payload for c2s look into a struct.
 * Callers: chunk_stream_receiver.c, decode_wire.c, mc_c2s_log.c.
 */

lc_status lc_parse_c2s_look(const uint8_t *data, size_t len, lc_c2s_look *out) {
  lc_buf b;
  lc_buf_init(&b, data, len);
  if (lc_buf_read_f32_le(&b, &out->yaw) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_f32_le(&b, &out->pitch) != LC_OK) return LC_ERR_TRUNCATED;
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

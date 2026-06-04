#include "../internal.h"
/* Good for: Decode Minecraft wire payload for sync entity position into a struct.
 * Callers: chunk_stream_receiver.c, decode_wire.c.
 */

lc_status lc_parse_sync_entity_position(const uint8_t *data, size_t len, lc_sync_entity_position *out) {
  lc_buf b;
  lc_buf_init(&b, data, len);
  if (lc_buf_read_varint(&b, &out->entity_id) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_f64_be(&b, &out->x) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_f64_be(&b, &out->y) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_f64_be(&b, &out->z) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_f64_be(&b, &out->dx) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_f64_be(&b, &out->dy) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_f64_be(&b, &out->dz) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_f32_be(&b, &out->yaw) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_f32_be(&b, &out->pitch) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_bool(&b, &out->on_ground) != LC_OK) return LC_ERR_TRUNCATED;
  return LC_OK;
}
/* Good for: One-line debug summary of lc_sync entity position (sniffer / decode tools).
 * Callers: decode_wire.c.
 */

int lc_sync_entity_position_to_string(const lc_sync_entity_position *p, char *buf, size_t buflen) {
  if (!p || !buf || buflen == 0) return 0;
  return lc_snprintf(buf, buflen,
                     "sync_entity_position{id=%d,pos=(%.3f,%.3f,%.3f),delta=(%.3f,%.3f,%.3f),"
                     "rot=(%.2f,%.2f),onGround=%u}",
                     p->entity_id, p->x, p->y, p->z, p->dx, p->dy, p->dz, p->yaw, p->pitch,
                     p->on_ground);
}

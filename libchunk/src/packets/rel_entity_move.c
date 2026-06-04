#include "../internal.h"
/* Good for: Decode Minecraft wire payload for rel entity move into a struct.
 * Callers: chunk_stream_receiver.c, decode_wire.c.
 */

lc_status lc_parse_rel_entity_move(const uint8_t *data, size_t len, lc_rel_entity_move *out) {
  lc_buf b;
  lc_buf_init(&b, data, len);
  if (lc_buf_read_varint(&b, &out->entity_id) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_i16_be(&b, &out->dx) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_i16_be(&b, &out->dy) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_i16_be(&b, &out->dz) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_bool(&b, &out->on_ground) != LC_OK) return LC_ERR_TRUNCATED;
  return LC_OK;
}
/* Good for: One-line debug summary of lc_rel entity move (sniffer / decode tools).
 * Callers: decode_wire.c.
 */

int lc_rel_entity_move_to_string(const lc_rel_entity_move *p, char *buf, size_t buflen) {
  if (!p || !buf || buflen == 0) return 0;
  return lc_snprintf(buf, buflen, "rel_entity_move{id=%d,d=(%d,%d,%d),onGround=%u}", p->entity_id,
                     (int)p->dx, (int)p->dy, (int)p->dz, p->on_ground);
}

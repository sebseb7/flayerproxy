#include "../internal.h"

lc_status lc_parse_entity_move_look(const uint8_t *data, size_t len, lc_entity_move_look *out) {
  lc_buf b;
  lc_buf_init(&b, data, len);
  if (lc_buf_read_varint(&b, &out->entity_id) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_i16_le(&b, &out->dx) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_i16_le(&b, &out->dy) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_i16_le(&b, &out->dz) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_i8(&b, &out->yaw) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_i8(&b, &out->pitch) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_bool(&b, &out->on_ground) != LC_OK) return LC_ERR_TRUNCATED;
  return LC_OK;
}

int lc_entity_move_look_to_string(const lc_entity_move_look *p, char *buf, size_t buflen) {
  if (!p || !buf || buflen == 0) return 0;
  return lc_snprintf(buf, buflen,
                     "entity_move_look{id=%d,d=(%d,%d,%d),yaw=%d,pitch=%d,onGround=%u}", p->entity_id,
                     (int)p->dx, (int)p->dy, (int)p->dz, (int)p->yaw, (int)p->pitch, p->on_ground);
}

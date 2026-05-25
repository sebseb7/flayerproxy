#include "../internal.h"

lc_status lc_parse_entity_head_rotation(const uint8_t *data, size_t len, lc_entity_head_rotation *out) {
  lc_buf b;
  lc_buf_init(&b, data, len);
  if (lc_buf_read_varint(&b, &out->entity_id) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_i8(&b, &out->head_yaw) != LC_OK) return LC_ERR_TRUNCATED;
  return LC_OK;
}

int lc_entity_head_rotation_to_string(const lc_entity_head_rotation *p, char *buf, size_t buflen) {
  if (!p || !buf || buflen == 0) return 0;
  return lc_snprintf(buf, buflen, "entity_head_rotation{id=%d,headYaw=%d}", p->entity_id,
                     (int)p->head_yaw);
}

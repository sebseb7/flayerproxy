#include "../internal.h"

lc_status lc_parse_entity_velocity(const uint8_t *data, size_t len, lc_entity_velocity *out) {
  lc_buf b;
  lc_buf_init(&b, data, len);
  if (lc_buf_read_varint(&b, &out->entity_id) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_lpvec3(&b, &out->velocity) != LC_OK) return LC_ERR_TRUNCATED;
  return LC_OK;
}

int lc_entity_velocity_to_string(const lc_entity_velocity *p, char *buf, size_t buflen) {
  if (!p || !buf || buflen == 0) return 0;
  return lc_snprintf(buf, buflen, "entity_velocity{id=%d,vel=(%.3f,%.3f,%.3f)}", p->entity_id,
                     p->velocity.x, p->velocity.y, p->velocity.z);
}

#include "../internal.h"

lc_status lc_parse_position(const uint8_t *data, size_t len, lc_position *out) {
  lc_buf b;
  lc_buf_init(&b, data, len);
  if (lc_buf_read_varint(&b, &out->teleport_id) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_f64_le(&b, &out->x) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_f64_le(&b, &out->y) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_f64_le(&b, &out->z) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_f64_le(&b, &out->dx) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_f64_le(&b, &out->dy) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_f64_le(&b, &out->dz) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_f32_le(&b, &out->yaw) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_f32_le(&b, &out->pitch) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_u32_le(&b, &out->flags) != LC_OK) return LC_ERR_TRUNCATED;
  return LC_OK;
}

int lc_position_to_string(const lc_position *p, char *buf, size_t buflen) {
  if (!p || !buf || buflen == 0) return 0;
  return lc_snprintf(buf, buflen,
                     "position{teleportId=%d,pos=(%.3f,%.3f,%.3f),delta=(%.3f,%.3f,%.3f),"
                     "rot=(%.2f,%.2f),flags=0x%x}",
                     p->teleport_id, p->x, p->y, p->z, p->dx, p->dy, p->dz, p->yaw, p->pitch,
                     p->flags);
}

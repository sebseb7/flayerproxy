#include "../internal.h"

lc_status lc_parse_block_change(const uint8_t *data, size_t len, lc_block_change *out) {
  lc_buf b;
  lc_buf_init(&b, data, len);
  if (lc_buf_read_position(&b, &out->location) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_varint(&b, &out->type) != LC_OK) return LC_ERR_TRUNCATED;
  return LC_OK;
}

int lc_block_change_to_string(const lc_block_change *p, char *buf, size_t buflen) {
  if (!p || !buf || buflen == 0) return 0;
  return lc_snprintf(buf, buflen, "block_change{pos=(%d,%d,%d),type=%d}", p->location.x, p->location.y,
                     p->location.z, p->type);
}

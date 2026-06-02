#include "../internal.h"

lc_status lc_parse_step_tick(const uint8_t *data, size_t len, lc_step_tick *out) {
  memset(out, 0, sizeof(*out));
  lc_buf b;
  lc_buf_init(&b, data, len);
  if (lc_buf_read_bool(&b, &out->skip_tick) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_remaining(&b) != 0) return LC_ERR_INVALID;
  return LC_OK;
}

int lc_step_tick_to_string(const lc_step_tick *p, char *buf, size_t buflen) {
  if (!p || !buf || buflen == 0) return 0;
  return lc_snprintf(buf, buflen, "step_tick{skipTick=%s}", p->skip_tick ? "true" : "false");
}

#include "../internal.h"

lc_status lc_parse_set_passengers(const uint8_t *data, size_t len, lc_set_passengers *out) {
  memset(out, 0, sizeof(*out));
  lc_buf b;
  lc_buf_init(&b, data, len);
  if (lc_buf_read_varint(&b, &out->entity_id) != LC_OK) return LC_ERR_TRUNCATED;
  int32_t count;
  if (lc_buf_read_varint(&b, &count) != LC_OK) return LC_ERR_TRUNCATED;
  if (count < 0) return LC_ERR_INVALID;
  out->passenger_count = (size_t)count;
  out->passengers = count ? (int32_t *)malloc((size_t)count * sizeof(int32_t)) : NULL;
  if (count && !out->passengers) return LC_ERR_OOM;
  for (int32_t i = 0; i < count; i++) {
    if (lc_buf_read_varint(&b, &out->passengers[i]) != LC_OK) {
      lc_set_passengers_free(out);
      return LC_ERR_TRUNCATED;
    }
  }
  return LC_OK;
}

void lc_set_passengers_free(lc_set_passengers *p) {
  free(p->passengers);
  p->passengers = NULL;
  p->passenger_count = 0;
}

int lc_set_passengers_to_string(const lc_set_passengers *p, char *buf, size_t buflen) {
  if (!p || !buf || buflen == 0) return 0;
  int w = lc_snprintf(buf, buflen, "set_passengers{vehicle=%d,passengers=[", p->entity_id);
  for (size_t i = 0; i < p->passenger_count; i++)
    w = lc_appendf(buf, buflen, w, "%s%d", i ? "," : "", p->passengers[i]);
  return lc_appendf(buf, buflen, w, "]}");
}

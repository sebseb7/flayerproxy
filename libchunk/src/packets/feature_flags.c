#include "../internal.h"

void lc_feature_flags_free(lc_feature_flags *p) {
  if (!p || !p->flags) return;
  for (size_t i = 0; i < p->count; i++) free(p->flags[i]);
  free(p->flags);
  p->flags = NULL;
  p->count = 0;
}

lc_status lc_parse_feature_flags(const uint8_t *data, size_t len, lc_feature_flags *out) {
  memset(out, 0, sizeof(*out));
  lc_buf b;
  lc_buf_init(&b, data, len);
  int32_t n;
  if (lc_buf_read_varint(&b, &n) != LC_OK) return LC_ERR_TRUNCATED;
  if (n < 0) return LC_ERR_INVALID;
  out->count = (size_t)n;
  if (n == 0) return LC_OK;
  out->flags = (char **)calloc((size_t)n, sizeof(char *));
  if (!out->flags) return LC_ERR_OOM;
  for (int32_t i = 0; i < n; i++) {
    if (lc_buf_read_string(&b, &out->flags[i]) != LC_OK) {
      lc_feature_flags_free(out);
      return LC_ERR_TRUNCATED;
    }
  }
  return LC_OK;
}

int lc_feature_flags_to_string(const lc_feature_flags *p, char *buf, size_t buflen) {
  if (!p || !buf || buflen == 0) return 0;
  if (p->count == 0) return lc_snprintf(buf, buflen, "feature_flags{count=0}");
  if (p->count == 1 && p->flags && p->flags[0]) {
    return lc_snprintf(buf, buflen, "feature_flags{count=1,flags=[%s]}", p->flags[0]);
  }
  const char *first = (p->flags && p->flags[0]) ? p->flags[0] : "";
  return lc_snprintf(buf, buflen, "feature_flags{count=%zu,flags=[%s,...]}", p->count, first);
}

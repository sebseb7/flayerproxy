#include "../internal.h"

lc_status lc_parse_bundle_delimiter(const uint8_t *data, size_t len) {
  if (len != 0) return LC_ERR_INVALID;
  (void)data;
  return LC_OK;
}

int lc_bundle_delimiter_to_string(char *buf, size_t buflen) {
  if (!buf || buflen == 0) return 0;
  return lc_snprintf(buf, buflen, "bundle_delimiter{}");
}

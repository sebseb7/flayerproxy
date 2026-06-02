#include "../internal.h"

lc_status lc_parse_finish_configuration(const uint8_t *data, size_t len) {
  if (len != 0) return LC_ERR_INVALID;
  (void)data;
  return LC_OK;
}

int lc_finish_configuration_to_string(char *buf, size_t buflen) {
  if (!buf || buflen == 0) return 0;
  return lc_snprintf(buf, buflen, "finish_configuration{}");
}

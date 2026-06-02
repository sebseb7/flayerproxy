#include "../internal.h"

void lc_custom_payload_free(lc_custom_payload *p) {
  if (!p) return;
  free(p->channel);
  p->channel = NULL;
  lc_byte_buf_free(&p->data);
}

lc_status lc_parse_custom_payload(const uint8_t *data, size_t len, lc_custom_payload *out) {
  memset(out, 0, sizeof(*out));
  lc_buf b;
  lc_buf_init(&b, data, len);
  if (lc_buf_read_string(&b, &out->channel) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_byte_array(&b, &out->data) != LC_OK) {
    free(out->channel);
    out->channel = NULL;
    return LC_ERR_TRUNCATED;
  }
  return LC_OK;
}

int lc_custom_payload_to_string(const lc_custom_payload *p, char *buf, size_t buflen) {
  if (!p || !buf || buflen == 0) return 0;
  if (p->channel && strcmp(p->channel, "minecraft:brand") == 0 && p->data.len > 0) {
    char brand[128];
    size_t n = p->data.len < sizeof brand - 1 ? p->data.len : sizeof brand - 1;
    memcpy(brand, p->data.data, n);
    brand[n] = '\0';
    return lc_snprintf(buf, buflen, "custom_payload{channel=%s,brand=\"%s\"}", p->channel, brand);
  }
  return lc_snprintf(buf, buflen, "custom_payload{channel=%s,data=%zuB}", p->channel ? p->channel : "",
                     p->data.len);
}

#include "../internal.h"

void lc_login_success_free(lc_login_success *p) {
  if (!p) return;
  free(p->username);
  p->username = NULL;
  if (p->properties) {
    for (size_t i = 0; i < p->property_count; i++) {
      free(p->properties[i].name);
      free(p->properties[i].value);
      free(p->properties[i].signature);
    }
    free(p->properties);
    p->properties = NULL;
  }
  p->property_count = 0;
}

static lc_status read_opt_string(lc_buf *b, char **out) {
  uint8_t present;
  if (lc_buf_read_bool(b, &present) != LC_OK) return LC_ERR_TRUNCATED;
  if (!present) {
    *out = NULL;
    return LC_OK;
  }
  return lc_buf_read_string(b, out);
}

lc_status lc_parse_success(const uint8_t *data, size_t len, lc_login_success *out) {
  memset(out, 0, sizeof(*out));
  lc_buf b;
  lc_buf_init(&b, data, len);
  if (lc_buf_read_uuid(&b, &out->uuid) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_string(&b, &out->username) != LC_OK) return LC_ERR_TRUNCATED;
  int32_t n;
  if (lc_buf_read_varint(&b, &n) != LC_OK) return LC_ERR_TRUNCATED;
  if (n < 0) return LC_ERR_INVALID;
  out->property_count = (size_t)n;
  if (n == 0) return LC_OK;
  out->properties = (lc_login_property *)calloc((size_t)n, sizeof(lc_login_property));
  if (!out->properties) return LC_ERR_OOM;
  for (int32_t i = 0; i < n; i++) {
    lc_login_property *prop = &out->properties[i];
    if (lc_buf_read_string(&b, &prop->name) != LC_OK) goto fail;
    if (lc_buf_read_string(&b, &prop->value) != LC_OK) goto fail;
    if (read_opt_string(&b, &prop->signature) != LC_OK) goto fail;
  }
  if (lc_buf_remaining(&b) != 0) {
    lc_login_success_free(out);
    return LC_ERR_INVALID;
  }
  return LC_OK;
fail:
  lc_login_success_free(out);
  return LC_ERR_TRUNCATED;
}

int lc_success_to_string(const lc_login_success *p, char *buf, size_t buflen) {
  if (!p || !buf || buflen == 0) return 0;
  char uuid[40];
  lc_uuid_to_string(&p->uuid, uuid, sizeof uuid);
  if (p->property_count == 0) {
    return lc_snprintf(buf, buflen, "success{uuid=%s,username=%s,properties=0}", uuid,
                       p->username ? p->username : "");
  }
  if (p->property_count == 1 && p->properties) {
    const lc_login_property *prop = &p->properties[0];
    return lc_snprintf(buf, buflen, "success{uuid=%s,username=%s,properties=1,name=%s}", uuid,
                       p->username ? p->username : "", prop->name ? prop->name : "");
  }
  return lc_snprintf(buf, buflen, "success{uuid=%s,username=%s,properties=%zu}", uuid,
                   p->username ? p->username : "", p->property_count);
}

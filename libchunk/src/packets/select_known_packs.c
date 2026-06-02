#include "../internal.h"

void lc_select_known_packs_free(lc_select_known_packs *p) {
  if (!p || !p->packs) return;
  for (size_t i = 0; i < p->count; i++) {
    free(p->packs[i].pack_namespace);
    free(p->packs[i].id);
    free(p->packs[i].version);
  }
  free(p->packs);
  p->packs = NULL;
  p->count = 0;
}

lc_status lc_parse_select_known_packs(const uint8_t *data, size_t len, lc_select_known_packs *out) {
  memset(out, 0, sizeof(*out));
  lc_buf b;
  lc_buf_init(&b, data, len);
  int32_t n;
  if (lc_buf_read_varint(&b, &n) != LC_OK) return LC_ERR_TRUNCATED;
  if (n < 0) return LC_ERR_INVALID;
  out->count = (size_t)n;
  if (n == 0) return LC_OK;
  out->packs = (lc_known_pack *)calloc((size_t)n, sizeof(lc_known_pack));
  if (!out->packs) return LC_ERR_OOM;
  for (int32_t i = 0; i < n; i++) {
    lc_known_pack *pk = &out->packs[i];
    if (lc_buf_read_string(&b, &pk->pack_namespace) != LC_OK) goto fail;
    if (lc_buf_read_string(&b, &pk->id) != LC_OK) goto fail;
    if (lc_buf_read_string(&b, &pk->version) != LC_OK) goto fail;
  }
  return LC_OK;
fail:
  lc_select_known_packs_free(out);
  return LC_ERR_TRUNCATED;
}

int lc_select_known_packs_to_string(const lc_select_known_packs *p, char *buf, size_t buflen) {
  if (!p || !buf || buflen == 0) return 0;
  if (p->count == 0) return lc_snprintf(buf, buflen, "select_known_packs{count=0}");
  if (p->count == 1 && p->packs) {
    const lc_known_pack *pk = &p->packs[0];
    return lc_snprintf(buf, buflen, "select_known_packs{count=1,packs=[%s:%s:%s]}",
                       pk->pack_namespace ? pk->pack_namespace : "",
                       pk->id ? pk->id : "", pk->version ? pk->version : "");
  }
  const lc_known_pack *pk = p->packs;
  return lc_snprintf(buf, buflen, "select_known_packs{count=%zu,packs=[%s:%s:%s,...]}", p->count,
                     pk && pk->pack_namespace ? pk->pack_namespace : "",
                     pk && pk->id ? pk->id : "", pk && pk->version ? pk->version : "");
}

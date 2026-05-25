#include "../internal.h"

void lc_registry_entry_arr_free(lc_registry_entry_arr *a) {
  if (!a->items) return;
  for (size_t i = 0; i < a->count; i++) {
    free(a->items[i].key);
    lc_byte_buf_free(&a->items[i].nbt);
  }
  free(a->items);
  a->items = NULL;
  a->count = 0;
}

lc_status lc_parse_registry_data(const uint8_t *data, size_t len, lc_registry_data *out) {
  memset(out, 0, sizeof(*out));
  lc_buf b;
  lc_buf_init(&b, data, len);
  if (lc_buf_read_string(&b, &out->id) != LC_OK) return LC_ERR_TRUNCATED;
  int32_t count;
  if (lc_buf_read_varint(&b, &count) != LC_OK) goto fail;
  if (count < 0) {
    free(out->id);
    out->id = NULL;
    return LC_ERR_INVALID;
  }
  out->entries.count = (size_t)count;
  out->entries.items = count ? (lc_registry_entry *)calloc((size_t)count, sizeof(lc_registry_entry)) : NULL;
  if (count && !out->entries.items) goto fail;
  for (int32_t i = 0; i < count; i++) {
    if (lc_buf_read_string(&b, &out->entries.items[i].key) != LC_OK) goto fail;
    uint8_t present;
    if (lc_nbt_read_anon_optional(&b, &out->entries.items[i].nbt, &present) != LC_OK) goto fail;
    if (!present) {
      out->entries.items[i].nbt.data = NULL;
      out->entries.items[i].nbt.len = 0;
    }
  }
  return LC_OK;
fail:
  lc_registry_data_free(out);
  return LC_ERR_TRUNCATED;
}

void lc_registry_data_free(lc_registry_data *p) {
  free(p->id);
  lc_registry_entry_arr_free(&p->entries);
  memset(p, 0, sizeof(*p));
}

int lc_registry_data_to_string(const lc_registry_data *p, char *buf, size_t buflen) {
  if (!p || !buf || buflen == 0) return 0;
  return lc_snprintf(buf, buflen, "registry_data{id=%s,entries=%zu (use decode_raw_dir for full dump)}",
                     p->id ? p->id : "?", p->entries.count);
}

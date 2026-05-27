#include "../internal.h"
/* Good for: Format one metadata entry for toString.
 * Callers: debug.c, entity_metadata.c (same file).
 */

static int lc_write_meta_value(const lc_metadata_entry *e, char *buf, size_t buflen, int w) {
  switch (e->kind) {
    case LC_META_BYTE:
      return lc_appendf(buf, buflen, w, "%d", (int)e->v.i8);
    case LC_META_INT:
    case LC_META_VARINT:
      return lc_appendf(buf, buflen, w, "%d", e->v.i32);
    case LC_META_LONG:
      return lc_appendf(buf, buflen, w, "%lld", (long long)e->v.i64);
    case LC_META_FLOAT:
      return lc_appendf(buf, buflen, w, "%g", e->v.f32);
    case LC_META_DOUBLE:
      return lc_appendf(buf, buflen, w, "%g", e->v.f64);
    case LC_META_BOOL:
      return lc_appendf(buf, buflen, w, "%s", e->v.boolean ? "true" : "false");
    case LC_META_STRING:
      return lc_appendf(buf, buflen, w, "\"%s\"", e->v.string ? e->v.string : "");
    case LC_META_RAW:
      return lc_appendf(buf, buflen, w, "<raw %zu bytes>", e->v.raw.len);
    default:
      return lc_appendf(buf, buflen, w, "?");
  }
}
/* Good for: Decode Minecraft wire payload for entity metadata into a struct.
 * Callers: chunk_stream_receiver.c, decode_wire.c.
 */

lc_status lc_parse_entity_metadata(const uint8_t *data, size_t len, lc_entity_metadata *out) {
  memset(out, 0, sizeof(*out));
  lc_buf b;
  lc_buf_init(&b, data, len);
  if (lc_buf_read_varint(&b, &out->entity_id) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_metadata_read_loop(&b, &out->metadata) != LC_OK) return LC_ERR_TRUNCATED;
  return LC_OK;
}
/* Good for: Release heap owned by lc_entity metadata.
 * Callers: chunk_stream_receiver.c, decode_wire.c.
 */

void lc_entity_metadata_free(lc_entity_metadata *p) {
  lc_metadata_arr_free(&p->metadata);
  memset(p, 0, sizeof(*p));
}
/* Good for: One-line debug summary of lc_entity metadata (sniffer / decode tools).
 * Callers: decode_wire.c.
 */

int lc_entity_metadata_to_string(const lc_entity_metadata *p, char *buf, size_t buflen) {
  if (!p || !buf || buflen == 0) return 0;
  int w = lc_snprintf(buf, buflen, "entity_metadata{id=%d,entries=[", p->entity_id);
  for (size_t i = 0; i < p->metadata.count; i++) {
    const lc_metadata_entry *e = &p->metadata.items[i];
    w = lc_appendf(buf, buflen, w, "%s{%u:%s=", i ? "," : "", e->key, e->type_name);
    w = lc_write_meta_value(e, buf, buflen, w);
    w = lc_appendf(buf, buflen, w, "}");
  }
  return lc_appendf(buf, buflen, w, "]}");
}

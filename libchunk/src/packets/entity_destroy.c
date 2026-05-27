#include "../internal.h"
/* Good for: Decode Minecraft wire payload for entity destroy into a struct.
 * Callers: chunk_stream_receiver.c, decode_wire.c.
 */

lc_status lc_parse_entity_destroy(const uint8_t *data, size_t len, lc_entity_destroy *out) {
  memset(out, 0, sizeof(*out));
  lc_buf b;
  lc_buf_init(&b, data, len);
  int32_t count;
  if (lc_buf_read_varint(&b, &count) != LC_OK) return LC_ERR_TRUNCATED;
  if (count < 0) return LC_ERR_INVALID;
  out->count = (size_t)count;
  out->entity_ids = count ? (int32_t *)malloc((size_t)count * sizeof(int32_t)) : NULL;
  if (count && !out->entity_ids) return LC_ERR_OOM;
  for (int32_t i = 0; i < count; i++) {
    if (lc_buf_read_varint(&b, &out->entity_ids[i]) != LC_OK) {
      lc_entity_destroy_free(out);
      return LC_ERR_TRUNCATED;
    }
  }
  return LC_OK;
}
/* Good for: Release heap owned by lc_entity destroy.
 * Callers: chunk_stream_receiver.c, decode_wire.c, entity_destroy.c (same file), packets.c.
 */

void lc_entity_destroy_free(lc_entity_destroy *p) {
  free(p->entity_ids);
  p->entity_ids = NULL;
  p->count = 0;
}
/* Good for: One-line debug summary of lc_entity destroy (sniffer / decode tools).
 * Callers: decode_wire.c.
 */

int lc_entity_destroy_to_string(const lc_entity_destroy *p, char *buf, size_t buflen) {
  if (!p || !buf || buflen == 0) return 0;
  int w = lc_snprintf(buf, buflen, "entity_destroy{ids=[");
  for (size_t i = 0; i < p->count; i++) w = lc_appendf(buf, buflen, w, "%s%d", i ? "," : "", p->entity_ids[i]);
  return lc_appendf(buf, buflen, w, "]}");
}

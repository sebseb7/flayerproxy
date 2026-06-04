#include "../internal.h"
/* Good for: Decode Minecraft wire payload for initialize world border into a struct.
 * Callers: decode_wire.c, play_stream.c.
 */

lc_status lc_parse_initialize_world_border(const uint8_t *data, size_t len, lc_initialize_world_border *out) {
  lc_buf b;
  lc_buf_init(&b, data, len);
  if (lc_buf_read_f64_be(&b, &out->x) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_f64_be(&b, &out->z) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_f64_be(&b, &out->old_diameter) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_f64_be(&b, &out->new_diameter) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_varint(&b, &out->speed) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_varint(&b, &out->portal_teleport_boundary) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_varint(&b, &out->warning_blocks) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_varint(&b, &out->warning_time) != LC_OK) return LC_ERR_TRUNCATED;
  return LC_OK;
}
/* Good for: One-line debug summary of lc_initialize world border (sniffer / decode tools).
 * Callers: decode_wire.c, play_stream.c.
 */

int lc_initialize_world_border_to_string(const lc_initialize_world_border *p, char *buf, size_t buflen) {
  if (!p || !buf || buflen == 0) return 0;
  return lc_snprintf(buf, buflen,
                     "initialize_world_border{center=(%.1f,%.1f),oldD=%.0f,newD=%.0f,speed=%d,"
                     "portalBoundary=%d,warnBlocks=%d,warnTime=%d}",
                     p->x, p->z, p->old_diameter, p->new_diameter, p->speed, p->portal_teleport_boundary,
                     p->warning_blocks, p->warning_time);
}

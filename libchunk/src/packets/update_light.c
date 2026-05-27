#include "../internal.h"
/* Good for: Decode Minecraft wire payload for update light into a struct.
 * Callers: chunk_stream_receiver.c, decode_raw_dir.c, decode_wire.c, mc_s2c_log.c.
 */

lc_status lc_parse_update_light(const uint8_t *data, size_t len, lc_update_light *out) {
  memset(out, 0, sizeof(*out));
  lc_buf b;
  lc_buf_init(&b, data, len);
  if (lc_buf_read_varint(&b, &out->chunk_x) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_varint(&b, &out->chunk_z) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_i64_array(&b, &out->sky_light_mask) != LC_OK) goto fail;
  if (lc_buf_read_i64_array(&b, &out->block_light_mask) != LC_OK) goto fail;
  if (lc_buf_read_i64_array(&b, &out->empty_sky_light_mask) != LC_OK) goto fail;
  if (lc_buf_read_i64_array(&b, &out->empty_block_light_mask) != LC_OK) goto fail;
  if (lc_buf_read_u8_grid(&b, &out->sky_light) != LC_OK) goto fail;
  if (lc_buf_read_u8_grid(&b, &out->block_light) != LC_OK) goto fail;
  return LC_OK;
fail:
  lc_update_light_free(out);
  return LC_ERR_TRUNCATED;
}
/* Good for: Release heap owned by lc_update light.
 * Callers: chunk_stream_receiver.c, decode_raw_dir.c, decode_wire.c, mc_s2c_log.c, packets.c, update_light.c (same file).
 */

void lc_update_light_free(lc_update_light *p) {
  if (!p) return;
  lc_i64_arr_free(&p->sky_light_mask);
  lc_i64_arr_free(&p->block_light_mask);
  lc_i64_arr_free(&p->empty_sky_light_mask);
  lc_i64_arr_free(&p->empty_block_light_mask);
  lc_u8_grid_free(&p->sky_light);
  lc_u8_grid_free(&p->block_light);
  memset(p, 0, sizeof(*p));
}
/* Good for: One-line debug summary of lc_update light (sniffer / decode tools).
 * Callers: decode_wire.c.
 */

int lc_update_light_to_string(const lc_update_light *p, char *buf, size_t buflen) {
  if (!p || !buf || buflen == 0) return 0;
  return lc_snprintf(buf, buflen,
                     "update_light{chunk=(%d,%d),skyMask=%zu,blockMask=%zu,skySections=%zu,blockSections=%zu}",
                     p->chunk_x, p->chunk_z, p->sky_light_mask.count, p->block_light_mask.count,
                     p->sky_light.row_count, p->block_light.row_count);
}

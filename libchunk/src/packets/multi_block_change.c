#include "../internal.h"
/* Good for: Decode Minecraft wire payload for multi block change into a struct.
 * Callers: chunk_stream_receiver.c, decode_wire.c.
 */

lc_status lc_parse_multi_block_change(const uint8_t *data, size_t len, lc_multi_block_change *out) {
  memset(out, 0, sizeof(*out));
  lc_buf b;
  lc_buf_init(&b, data, len);
  static const lc_bitfield_spec spec[] = {{22, 1}, {22, 1}, {20, 1}};
  int32_t vals[3];
  if (lc_buf_read_bitfield(&b, spec, 3, vals) != LC_OK) return LC_ERR_TRUNCATED;
  out->chunk_coordinates.x = vals[0];
  out->chunk_coordinates.z = vals[1];
  out->chunk_coordinates.y = vals[2];
  int32_t count;
  if (lc_buf_read_varint(&b, &count) != LC_OK) return LC_ERR_TRUNCATED;
  if (count < 0) return LC_ERR_INVALID;
  out->record_count = (size_t)count;
  out->records = count ? (int32_t *)malloc((size_t)count * sizeof(int32_t)) : NULL;
  if (count && !out->records) return LC_ERR_OOM;
  for (int32_t i = 0; i < count; i++) {
    if (lc_buf_read_varint(&b, &out->records[i]) != LC_OK) {
      lc_multi_block_change_free(out);
      return LC_ERR_TRUNCATED;
    }
  }
  return LC_OK;
}
/* Good for: Release heap owned by lc_multi block change.
 * Callers: chunk_stream_receiver.c, decode_wire.c, multi_block_change.c (same file), packets.c.
 */

void lc_multi_block_change_free(lc_multi_block_change *p) {
  free(p->records);
  p->records = NULL;
  p->record_count = 0;
}
/* Good for: Unpack packed long from multi_block_change record.
 * Callers: debug.c, multi_block_change.c (same file).
 */

static void lc_unpack_multi_block_record(int32_t record, int *lx, int *ly, int *lz, int32_t *state_id) {
  *lz = (record >> 4) & 0x0f;
  *lx = (record >> 8) & 0x0f;
  *ly = record & 0x0f;
  *state_id = (int32_t)((uint32_t)record >> 12);
}
/* Good for: One-line debug summary of lc_multi block change (sniffer / decode tools).
 * Callers: decode_wire.c.
 */

int lc_multi_block_change_to_string(const lc_multi_block_change *p, char *buf, size_t buflen) {
  if (!p || !buf || buflen == 0) return 0;
  const int32_t cx = p->chunk_coordinates.x;
  const int32_t cz = p->chunk_coordinates.z;
  const int32_t sec_y = p->chunk_coordinates.y;
  int w = lc_snprintf(buf, buflen,
                      "multi_block_change{chunk=(%d,%d),sectionY=%d,count=%zu,changes=[", cx, cz,
                      sec_y, p->record_count);
  for (size_t i = 0; i < p->record_count; i++) {
    int lx, ly, lz;
    int32_t state_id;
    lc_unpack_multi_block_record(p->records[i], &lx, &ly, &lz, &state_id);
    const int32_t wx = cx * 16 + lx;
    const int32_t wz = cz * 16 + lz;
    const int32_t wy = sec_y * 16 + ly;
    w = lc_appendf(buf, buflen, w, "%spos=(%d,%d,%d) state=%d", i ? "," : "", wx, wy, wz,
                   state_id);
  }
  return lc_appendf(buf, buflen, w, "]}");
}

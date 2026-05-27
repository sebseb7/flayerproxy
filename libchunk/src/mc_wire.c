#include "mc_wire.h"

#include "internal.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

static lc_status write_bitfield(mc_buf *b, const lc_bitfield_spec *fields, size_t nfields,
                                const int32_t *vals) {
  int bits_left = 0;
  uint8_t cur = 0;
  for (size_t fi = 0; fi < nfields; fi++) {
    int need = fields[fi].size;
    int val = vals[fi];
    if (fields[fi].signed_f && val < 0) val += (1 << fields[fi].size);
    int written = 0;
    while (written < need) {
      if (bits_left == 0) {
        if (mc_buf_u8(b, 0) != LC_OK) return LC_ERR_OOM;
        cur = b->data[b->len - 1];
        bits_left = 8;
      }
      int take = need - written;
      if (take > bits_left) take = bits_left;
      int shift = need - written - take;
      int mask = (1 << take) - 1;
      int chunk = (val >> shift) & mask;
      cur |= (uint8_t)(chunk << (bits_left - take));
      b->data[b->len - 1] = cur;
      bits_left -= take;
      written += take;
    }
  }
  if (bits_left > 0 && b->len > 0) b->data[b->len - 1] = cur;
  return LC_OK;
}
/* Good for: Outbound Minecraft wire buffer framing (server tools).
 * Callers: mc_auth_offline.c, mc_reference_client.c, mc_server_common.c, mc_static_config.c, mc_static_server.c, mc_wire.c (same file), mc_wire_templates.c, packets_write.c.
 */

void mc_buf_free(mc_buf *b) {
  free(b->data);
  b->data = NULL;
  b->len = b->cap = 0;
}
/* Good for: Outbound Minecraft wire buffer framing (server tools).
 * Callers: mc_wire.c (same file).
 */

lc_status mc_buf_reserve(mc_buf *b, size_t need) {
  if (need <= b->cap) return LC_OK;
  size_t ncap = b->cap ? b->cap : 64;
  while (ncap < need) {
    if (ncap > (size_t)(1024 * 1024 * 16)) return LC_ERR_OOM;
    ncap *= 2;
  }
  uint8_t *p = (uint8_t *)realloc(b->data, ncap);
  if (!p) return LC_ERR_OOM;
  b->data = p;
  b->cap = ncap;
  return LC_OK;
}
/* Good for: Outbound Minecraft wire buffer framing (server tools).
 * Callers: mc_server_common.c, mc_static_config.c, mc_wire.c (same file), mc_wire_templates.c, packets_write.c.
 */

lc_status mc_buf_write(mc_buf *b, const void *src, size_t n) {
  if (mc_buf_reserve(b, b->len + n) != LC_OK) return LC_ERR_OOM;
  memcpy(b->data + b->len, src, n);
  b->len += n;
  return LC_OK;
}
/* Good for: Outbound Minecraft wire buffer framing (server tools).
 * Callers: mc_reference_client.c, mc_spectator.c, mc_wire.c (same file), mc_wire_templates.c, packets_write.c.
 */

lc_status mc_buf_u8(mc_buf *b, uint8_t v) { return mc_buf_write(b, &v, 1); }
/* Good for: Outbound Minecraft wire buffer framing (server tools).
 * Callers: packets_write.c.
 */

lc_status mc_buf_i16_be(mc_buf *b, int16_t v) {
  uint8_t x[2] = {(uint8_t)((uint16_t)v >> 8), (uint8_t)v};
  return mc_buf_write(b, x, 2);
}
/* Good for: Outbound Minecraft wire buffer framing (server tools).
 * Callers: mc_reference_client.c, mc_spectator.c, mc_wire_templates.c, packets_write.c.
 */

lc_status mc_buf_i32_be(mc_buf *b, int32_t v) {
  uint8_t x[4] = {(uint8_t)((uint32_t)v >> 24), (uint8_t)((uint32_t)v >> 16), (uint8_t)((uint32_t)v >> 8),
                  (uint8_t)v};
  return mc_buf_write(b, x, 4);
}
/* Good for: Outbound Minecraft wire buffer framing (server tools).
 * Callers: mc_server_common.c, packets_write.c.
 */

lc_status mc_buf_i64_be(mc_buf *b, int64_t v) {
  uint8_t x[8];
  for (int i = 7; i >= 0; i--) {
    x[i] = (uint8_t)v;
    v >>= 8;
  }
  return mc_buf_write(b, x, 8);
}
/* Good for: Outbound Minecraft wire buffer framing (server tools).
 * Callers: mc_reference_client.c, mc_spectator.c, packets_write.c.
 */

lc_status mc_buf_f32_be(mc_buf *b, float v) {
  uint32_t bits;
  memcpy(&bits, &v, sizeof(bits));
  uint8_t x[4] = {(uint8_t)(bits >> 24), (uint8_t)(bits >> 16), (uint8_t)(bits >> 8), (uint8_t)bits};
  return mc_buf_write(b, x, 4);
}
/* Good for: Outbound Minecraft wire buffer framing (server tools).
 * Callers: mc_spectator.c, packets_write.c.
 */

lc_status mc_buf_f64_be(mc_buf *b, double v) {
  uint64_t bits;
  memcpy(&bits, &v, sizeof(bits));
  uint8_t x[8];
  for (int i = 7; i >= 0; i--) {
    x[i] = (uint8_t)bits;
    bits >>= 8;
  }
  return mc_buf_write(b, x, 8);
}
/* Good for: Outbound Minecraft wire buffer framing (server tools).
 * Callers: mc_auth_offline.c, mc_reference_client.c, mc_server_common.c, mc_spectator.c, mc_static_config.c, mc_static_server.c, mc_wire.c (same file), mc_wire_templates.c, packets_write.c.
 */

lc_status mc_buf_varint(mc_buf *b, int32_t value) {
  uint32_t uv = (uint32_t)value;
  do {
    uint8_t temp = (uint8_t)(uv & 0x7f);
    uv >>= 7;
    if (uv != 0) temp |= 0x80;
    if (mc_buf_u8(b, temp) != LC_OK) return LC_ERR_OOM;
  } while (uv != 0);
  return LC_OK;
}
/* Good for: Outbound Minecraft wire buffer framing (server tools).
 * Callers: mc_auth_offline.c, mc_reference_client.c, mc_server_common.c, mc_spectator.c, mc_static_config.c, mc_wire_templates.c, packets_write.c.
 */

lc_status mc_buf_string(mc_buf *b, const char *s) {
  size_t slen = s ? strlen(s) : 0;
  if (slen > (size_t)INT32_MAX) return LC_ERR_INVALID;
  if (mc_buf_varint(b, (int32_t)slen) != LC_OK) return LC_ERR_OOM;
  return slen ? mc_buf_write(b, s, slen) : LC_OK;
}
/* Good for: Outbound Minecraft wire buffer framing (server tools).
 * Callers: mc_auth_offline.c, mc_reference_client.c, mc_wire_templates.c.
 */

lc_status mc_buf_uuid(mc_buf *b, const uint8_t uuid[16]) { return mc_buf_write(b, uuid, 16); }
/* Good for: Outbound Minecraft wire buffer framing (server tools).
 * Callers: mc_static_server.c.
 */

lc_status mc_buf_block_pos(mc_buf *b, const lc_block_pos *p) {
  static const lc_bitfield_spec spec[] = {{26, 1}, {26, 1}, {12, 1}};
  int32_t vals[3] = {p->x, p->z, p->y};
  return write_bitfield(b, spec, 3, vals);
}
/* Good for: Outbound Minecraft wire buffer framing (server tools).
 * Callers: mc_server_common.c.
 */

lc_status mc_buf_frame(mc_buf *b, int32_t pkt_id, const uint8_t *payload, size_t payload_len) {
  mc_buf inner;
  memset(&inner, 0, sizeof(inner));
  if (mc_buf_varint(&inner, pkt_id) != LC_OK) goto fail;
  if (payload_len && mc_buf_write(&inner, payload, payload_len) != LC_OK) goto fail;
  if (mc_buf_varint(b, (int32_t)inner.len) != LC_OK) goto fail;
  if (mc_buf_write(b, inner.data, inner.len) != LC_OK) goto fail;
  mc_buf_free(&inner);
  return LC_OK;
fail:
  mc_buf_free(&inner);
  return LC_ERR_OOM;
}

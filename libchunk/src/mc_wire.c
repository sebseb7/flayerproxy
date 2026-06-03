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

static lc_status mc_buf_to_byte_buf(const mc_buf *src, lc_byte_buf *out) {
  if (!src || !out) return LC_ERR_INVALID;
  lc_byte_buf_free(out);
  out->data = NULL;
  out->len = 0;
  if (src->len == 0) return LC_OK;
  out->data = (uint8_t *)malloc(src->len);
  if (!out->data) return LC_ERR_OOM;
  memcpy(out->data, src->data, src->len);
  out->len = src->len;
  return LC_OK;
}

lc_status lc_try_read_frame(const uint8_t *data, size_t len, lc_frame_view *out) {
  if (!data || !out) return LC_ERR_INVALID;
  out->packet_id = 0;
  out->payload = NULL;
  out->payload_len = 0;
  out->frame_bytes = 0;

  lc_buf lb;
  lc_buf_init(&lb, data, len);
  int32_t body_len;
  if (lc_buf_read_varint(&lb, &body_len) != LC_OK) return LC_ERR_TRUNCATED;
  if (body_len < 0) return LC_ERR_INVALID;

  size_t body_off = lb.off;
  if ((size_t)body_len > len - body_off) return LC_ERR_TRUNCATED;

  lc_buf pb;
  lc_buf_init(&pb, data + body_off, (size_t)body_len);
  int32_t pkt_id;
  if (lc_buf_read_varint(&pb, &pkt_id) != LC_OK) return LC_ERR_INVALID;

  out->packet_id = pkt_id;
  out->payload = data + body_off + pb.off;
  out->payload_len = (size_t)body_len - pb.off;
  out->frame_bytes = body_off + (size_t)body_len;
  return LC_OK;
}

lc_status lc_build_frame(int32_t pkt_id, const uint8_t *payload, size_t payload_len, lc_byte_buf *out) {
  mc_buf frame;
  memset(&frame, 0, sizeof frame);
  lc_status st = mc_buf_frame(&frame, pkt_id, payload, payload_len);
  if (st != LC_OK) {
    mc_buf_free(&frame);
    return st;
  }
  st = mc_buf_to_byte_buf(&frame, out);
  mc_buf_free(&frame);
  return st;
}

lc_status lc_write_string(const char *s, lc_byte_buf *out) {
  mc_buf b;
  memset(&b, 0, sizeof b);
  lc_status st = mc_buf_string(&b, s);
  if (st != LC_OK) {
    mc_buf_free(&b);
    return st;
  }
  st = mc_buf_to_byte_buf(&b, out);
  mc_buf_free(&b);
  return st;
}

lc_status lc_read_string_at(const uint8_t *data, size_t len, size_t offset, char **out_str, size_t *out_next) {
  if (!data || !out_str || !out_next) return LC_ERR_INVALID;
  if (offset >= len) return LC_ERR_TRUNCATED;
  lc_buf b;
  lc_buf_init(&b, data + offset, len - offset);
  char *s = NULL;
  lc_status st = lc_buf_read_string(&b, &s);
  if (st != LC_OK) return st;
  *out_str = s;
  *out_next = offset + b.off;
  return LC_OK;
}

lc_status lc_read_varint_at(const uint8_t *data, size_t len, size_t *offset, int32_t *out) {
  if (!data || !offset || !out) return LC_ERR_INVALID;
  if (*offset >= len) return LC_ERR_TRUNCATED;
  lc_buf b;
  lc_buf_init(&b, data + *offset, len - *offset);
  if (lc_buf_read_varint(&b, out) != LC_OK) return LC_ERR_TRUNCATED;
  *offset += b.off;
  return LC_OK;
}

lc_status lc_write_varint(int32_t value, lc_byte_buf *out) {
  mc_buf b;
  memset(&b, 0, sizeof b);
  lc_status st = mc_buf_varint(&b, value);
  if (st != LC_OK) {
    mc_buf_free(&b);
    return st;
  }
  st = mc_buf_to_byte_buf(&b, out);
  mc_buf_free(&b);
  return st;
}

#define LC_FRAME_READER_MAX (16u * 1024u * 1024u)

static lc_status frame_reader_reserve(lc_frame_reader *r, size_t need) {
  if (need <= r->cap) return LC_OK;
  size_t cap = r->cap ? r->cap : 4096;
  while (cap < need) {
    if (cap > LC_FRAME_READER_MAX) return LC_ERR_INVALID;
    cap *= 2;
  }
  uint8_t *p = (uint8_t *)realloc(r->data, cap);
  if (!p) return LC_ERR_OOM;
  r->data = p;
  r->cap = cap;
  return LC_OK;
}

void lc_frame_reader_init(lc_frame_reader *r) {
  if (r) memset(r, 0, sizeof *r);
}

void lc_frame_reader_reset(lc_frame_reader *r) {
  if (r) r->len = 0;
}

void lc_frame_reader_free(lc_frame_reader *r) {
  if (!r) return;
  free(r->data);
  r->data = NULL;
  r->len = 0;
  r->cap = 0;
}

lc_status lc_frame_reader_feed(lc_frame_reader *r, const uint8_t *chunk, size_t chunk_len,
                               lc_frame_callback cb, void *ctx) {
  if (!r) return LC_ERR_INVALID;
  if (chunk_len > 0) {
    if (!chunk) return LC_ERR_INVALID;
    if (r->len + chunk_len > LC_FRAME_READER_MAX) return LC_ERR_INVALID;
    if (frame_reader_reserve(r, r->len + chunk_len) != LC_OK) return LC_ERR_OOM;
    memcpy(r->data + r->len, chunk, chunk_len);
    r->len += chunk_len;
  }

  for (;;) {
    lc_frame_view fv;
    lc_status st = lc_try_read_frame(r->data, r->len, &fv);
    if (st == LC_ERR_TRUNCATED) return LC_OK;
    if (st != LC_OK) return st;
    if (cb) cb(ctx, fv.packet_id, fv.payload, fv.payload_len);
    size_t remain = r->len - fv.frame_bytes;
    if (remain > 0) memmove(r->data, r->data + fv.frame_bytes, remain);
    r->len = remain;
  }
}

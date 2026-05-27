#include "internal.h"
#include <stdarg.h>
#include <stdio.h>

lc_status lc_skip_packet_id(const uint8_t *data, size_t len, const uint8_t **payload, size_t *payload_len) {
  lc_buf b;
  lc_buf_init(&b, data, len);
  int32_t id;
  lc_status st = lc_buf_read_varint(&b, &id);
  if (st != LC_OK) return st;
  (void)id;
  if (payload) *payload = data + b.off;
  if (payload_len) *payload_len = len - b.off;
  return LC_OK;
}

lc_status lc_buf_read_u8(lc_buf *b, uint8_t *out) {
  if (lc_buf_need(b, 1) != LC_OK) return LC_ERR_TRUNCATED;
  *out = b->data[b->off++];
  return LC_OK;
}

lc_status lc_buf_read_i8(lc_buf *b, int8_t *out) {
  uint8_t v;
  if (lc_buf_read_u8(b, &v) != LC_OK) return LC_ERR_TRUNCATED;
  *out = (int8_t)v;
  return LC_OK;
}

lc_status lc_buf_read_u16_le(lc_buf *b, uint16_t *out) {
  if (lc_buf_need(b, 2) != LC_OK) return LC_ERR_TRUNCATED;
  *out = (uint16_t)b->data[b->off] | ((uint16_t)b->data[b->off + 1] << 8);
  b->off += 2;
  return LC_OK;
}

lc_status lc_buf_read_u16_be(lc_buf *b, uint16_t *out) {
  if (lc_buf_need(b, 2) != LC_OK) return LC_ERR_TRUNCATED;
  *out = ((uint16_t)b->data[b->off] << 8) | (uint16_t)b->data[b->off + 1];
  b->off += 2;
  return LC_OK;
}

lc_status lc_buf_read_u32_be(lc_buf *b, uint32_t *out) {
  int32_t v;
  if (lc_buf_read_i32_be(b, &v) != LC_OK) return LC_ERR_TRUNCATED;
  *out = (uint32_t)v;
  return LC_OK;
}

lc_status lc_buf_read_i16_le(lc_buf *b, int16_t *out) {
  uint16_t v;
  if (lc_buf_read_u16_le(b, &v) != LC_OK) return LC_ERR_TRUNCATED;
  *out = (int16_t)v;
  return LC_OK;
}

lc_status lc_buf_read_i16_be(lc_buf *b, int16_t *out) {
  if (lc_buf_need(b, 2) != LC_OK) return LC_ERR_TRUNCATED;
  uint16_t v = ((uint16_t)b->data[b->off] << 8) | (uint16_t)b->data[b->off + 1];
  b->off += 2;
  *out = (int16_t)v;
  return LC_OK;
}

lc_status lc_buf_read_i32_le(lc_buf *b, int32_t *out) {
  if (lc_buf_need(b, 4) != LC_OK) return LC_ERR_TRUNCATED;
  uint32_t v = (uint32_t)b->data[b->off] | ((uint32_t)b->data[b->off + 1] << 8) |
               ((uint32_t)b->data[b->off + 2] << 16) | ((uint32_t)b->data[b->off + 3] << 24);
  b->off += 4;
  *out = (int32_t)v;
  return LC_OK;
}

lc_status lc_buf_read_i32_be(lc_buf *b, int32_t *out) {
  if (lc_buf_need(b, 4) != LC_OK) return LC_ERR_TRUNCATED;
  uint32_t v = ((uint32_t)b->data[b->off] << 24) | ((uint32_t)b->data[b->off + 1] << 16) |
               ((uint32_t)b->data[b->off + 2] << 8) | (uint32_t)b->data[b->off + 3];
  b->off += 4;
  *out = (int32_t)v;
  return LC_OK;
}

lc_status lc_buf_read_u32_le(lc_buf *b, uint32_t *out) {
  int32_t v;
  if (lc_buf_read_i32_le(b, &v) != LC_OK) return LC_ERR_TRUNCATED;
  *out = (uint32_t)v;
  return LC_OK;
}

lc_status lc_buf_read_i64_le(lc_buf *b, int64_t *out) {
  if (lc_buf_need(b, 8) != LC_OK) return LC_ERR_TRUNCATED;
  uint64_t v = 0;
  for (int i = 0; i < 8; i++) v |= (uint64_t)b->data[b->off + i] << (8 * i);
  b->off += 8;
  *out = (int64_t)v;
  return LC_OK;
}

lc_status lc_buf_read_f32_le(lc_buf *b, float *out) {
  /* Minecraft protocol uses big-endian IEEE floats (protodef f32/f64). */
  if (lc_buf_need(b, 4) != LC_OK) return LC_ERR_TRUNCATED;
  uint32_t bits = ((uint32_t)b->data[b->off] << 24) | ((uint32_t)b->data[b->off + 1] << 16) |
                  ((uint32_t)b->data[b->off + 2] << 8) | (uint32_t)b->data[b->off + 3];
  b->off += 4;
  memcpy(out, &bits, sizeof(float));
  return LC_OK;
}

lc_status lc_buf_read_f64_le(lc_buf *b, double *out) {
  if (lc_buf_need(b, 8) != LC_OK) return LC_ERR_TRUNCATED;
  uint64_t bits = 0;
  for (int i = 0; i < 8; i++) bits = (bits << 8) | b->data[b->off + i];
  b->off += 8;
  memcpy(out, &bits, sizeof(double));
  return LC_OK;
}

lc_status lc_buf_read_bool(lc_buf *b, uint8_t *out) {
  return lc_buf_read_u8(b, out);
}

lc_status lc_buf_read_varint(lc_buf *b, int32_t *out) {
  int32_t value = 0;
  int position = 0;
  while (1) {
    uint8_t byte;
    if (lc_buf_read_u8(b, &byte) != LC_OK) return LC_ERR_TRUNCATED;
    value |= (int32_t)(byte & 0x7f) << position;
    if ((byte & 0x80) == 0) break;
    position += 7;
    if (position >= 32) return LC_ERR_INVALID;
  }
  *out = value;
  return LC_OK;
}

lc_status lc_buf_read_varlong(lc_buf *b, int64_t *out) {
  int64_t value = 0;
  int position = 0;
  while (1) {
    uint8_t byte;
    if (lc_buf_read_u8(b, &byte) != LC_OK) return LC_ERR_TRUNCATED;
    value |= (int64_t)(byte & 0x7f) << position;
    if ((byte & 0x80) == 0) break;
    position += 7;
    if (position >= 64) return LC_ERR_INVALID;
  }
  *out = value;
  return LC_OK;
}

lc_status lc_buf_read_string(lc_buf *b, char **out) {
  int32_t len;
  if (lc_buf_read_varint(b, &len) != LC_OK) return LC_ERR_TRUNCATED;
  if (len < 0 || (size_t)len > lc_buf_remaining(b)) return LC_ERR_TRUNCATED;
  char *s = (char *)malloc((size_t)len + 1);
  if (!s) return LC_ERR_OOM;
  if (len > 0) memcpy(s, b->data + b->off, (size_t)len);
  s[len] = '\0';
  b->off += (size_t)len;
  *out = s;
  return LC_OK;
}

lc_status lc_buf_read_bytes(lc_buf *b, size_t n, lc_byte_buf *out) {
  if (lc_buf_need(b, n) != LC_OK) return LC_ERR_TRUNCATED;
  uint8_t *copy = n ? (uint8_t *)malloc(n) : NULL;
  if (n && !copy) return LC_ERR_OOM;
  if (n) memcpy(copy, b->data + b->off, n);
  b->off += n;
  out->data = copy;
  out->len = n;
  return LC_OK;
}

lc_status lc_buf_read_byte_array(lc_buf *b, lc_byte_buf *out) {
  int32_t len;
  if (lc_buf_read_varint(b, &len) != LC_OK) return LC_ERR_TRUNCATED;
  if (len < 0) return LC_ERR_INVALID;
  return lc_buf_read_bytes(b, (size_t)len, out);
}

lc_status lc_buf_read_uuid(lc_buf *b, lc_uuid *out) {
  if (lc_buf_need(b, 16) != LC_OK) return LC_ERR_TRUNCATED;
  memcpy(out->bytes, b->data + b->off, 16);
  b->off += 16;
  return LC_OK;
}

lc_status lc_buf_read_bitfield(lc_buf *b, const lc_bitfield_spec *fields, size_t nfields, int32_t *out_vals) {
  int bits_left = 0;
  uint8_t cur = 0;
  for (size_t fi = 0; fi < nfields; fi++) {
    int need = fields[fi].size;
    int val = 0;
    int got = 0;
    while (got < need) {
      if (bits_left == 0) {
        if (lc_buf_read_u8(b, &cur) != LC_OK) return LC_ERR_TRUNCATED;
        bits_left = 8;
      }
      int take = need - got;
      if (take > bits_left) take = bits_left;
      int shift = bits_left - take;
      int mask = (1 << take) - 1;
      val = (val << take) | ((cur >> shift) & mask);
      bits_left -= take;
      got += take;
    }
    if (fields[fi].signed_f && val >= (1 << (fields[fi].size - 1))) {
      val -= (1 << fields[fi].size);
    }
    out_vals[fi] = val;
  }
  return LC_OK;
}

lc_status lc_buf_read_position(lc_buf *b, lc_block_pos *out) {
  static const lc_bitfield_spec spec[] = {
      {26, 1}, {26, 1}, {12, 1},
  };
  int32_t vals[3];
  lc_status st = lc_buf_read_bitfield(b, spec, 3, vals);
  if (st != LC_OK) return st;
  out->x = vals[0];
  out->z = vals[1];
  out->y = vals[2];
  return LC_OK;
}

static double lc_lpvec3_unpack(uint64_t packed, int shift) {
  const int DATA_BITS_MASK = 32767;
  const double MAX_Q = 32766.0;
  int v = (int)((packed >> shift) & DATA_BITS_MASK);
  if (v > 32766) v = 32766;
  return (v * 2.0) / MAX_Q - 1.0;
}

lc_status lc_buf_read_lpvec3(lc_buf *b, lc_vec3 *out) {
  uint8_t a;
  if (lc_buf_read_u8(b, &a) != LC_OK) return LC_ERR_TRUNCATED;
  if (a == 0) {
    out->x = out->y = out->z = 0;
    return LC_OK;
  }
  if (lc_buf_need(b, 5) != LC_OK) return LC_ERR_TRUNCATED;
  uint8_t b1 = b->data[b->off + 1];
  uint32_t c = (uint32_t)b->data[b->off + 2] | ((uint32_t)b->data[b->off + 3] << 8) |
               ((uint32_t)b->data[b->off + 4] << 16);
  b->off += 5;

  uint64_t packed = ((uint64_t)c << 16) | ((uint64_t)b1 << 8) | a;
  int scale = a & 3;
  if (a & 4) {
    int32_t extra;
    if (lc_buf_read_varint(b, &extra) != LC_OK) return LC_ERR_TRUNCATED;
    scale = extra * 4 + scale;
  }

  out->x = lc_lpvec3_unpack(packed, 3) * scale;
  out->y = lc_lpvec3_unpack(packed, 18) * scale;
  out->z = lc_lpvec3_unpack(packed, 33) * scale;
  return LC_OK;
}

lc_status lc_buf_read_i64_be(lc_buf *b, int64_t *out) {
  if (lc_buf_need(b, 8) != LC_OK) return LC_ERR_TRUNCATED;
  uint64_t v = 0;
  for (int i = 0; i < 8; i++) v = (v << 8) | (uint64_t)b->data[b->off + i];
  b->off += 8;
  *out = (int64_t)v;
  return LC_OK;
}

lc_status lc_buf_read_i64_array(lc_buf *b, lc_i64_arr *out) {
  int32_t count;
  if (lc_buf_read_varint(b, &count) != LC_OK) return LC_ERR_TRUNCATED;
  if (count < 0) return LC_ERR_INVALID;
  out->count = (size_t)count;
  out->values = count ? (int64_t *)calloc((size_t)count, sizeof(int64_t)) : NULL;
  if (count && !out->values) return LC_ERR_OOM;
  for (int32_t i = 0; i < count; i++) {
    if (lc_buf_read_i64_be(b, &out->values[i]) != LC_OK) {
      lc_i64_arr_free(out);
      return LC_ERR_TRUNCATED;
    }
  }
  return LC_OK;
}

lc_status lc_buf_read_u8_array(lc_buf *b, lc_u8_arr *out) {
  int32_t count;
  if (lc_buf_read_varint(b, &count) != LC_OK) return LC_ERR_TRUNCATED;
  if (count < 0) return LC_ERR_INVALID;
  out->count = (size_t)count;
  out->values = count ? (uint8_t *)malloc((size_t)count) : NULL;
  if (count && !out->values) return LC_ERR_OOM;
  if (count) {
    if (lc_buf_need(b, (size_t)count) != LC_OK) {
      free(out->values);
      out->values = NULL;
      out->count = 0;
      return LC_ERR_TRUNCATED;
    }
    memcpy(out->values, b->data + b->off, (size_t)count);
    b->off += (size_t)count;
  }
  return LC_OK;
}

lc_status lc_buf_read_u8_grid(lc_buf *b, lc_u8_grid *out) {
  int32_t outer;
  if (lc_buf_read_varint(b, &outer) != LC_OK) return LC_ERR_TRUNCATED;
  if (outer < 0) return LC_ERR_INVALID;
  out->row_count = (size_t)outer;
  out->rows = outer ? (uint8_t **)calloc((size_t)outer, sizeof(uint8_t *)) : NULL;
  out->row_lens = outer ? (size_t *)calloc((size_t)outer, sizeof(size_t)) : NULL;
  if (outer && (!out->rows || !out->row_lens)) {
    lc_u8_grid_free(out);
    return LC_ERR_OOM;
  }
  for (int32_t i = 0; i < outer; i++) {
    lc_u8_arr row;
    if (lc_buf_read_u8_array(b, &row) != LC_OK) {
      lc_u8_grid_free(out);
      return LC_ERR_TRUNCATED;
    }
    out->rows[i] = row.values;
    out->row_lens[i] = row.count;
  }
  return LC_OK;
}

void lc_byte_buf_free(lc_byte_buf *b) {
  free(b->data);
  b->data = NULL;
  b->len = 0;
}

void lc_i64_arr_free(lc_i64_arr *a) {
  free(a->values);
  a->values = NULL;
  a->count = 0;
}

void lc_u8_grid_free(lc_u8_grid *g) {
  if (g->rows) {
    for (size_t i = 0; i < g->row_count; i++) free(g->rows[i]);
    free(g->rows);
    free(g->row_lens);
  }
  g->rows = NULL;
  g->row_lens = NULL;
  g->row_count = 0;
}

int lc_snprintf(char *buf, size_t buflen, const char *fmt, ...) {
  if (!buf || buflen == 0) return 0;
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(buf, buflen, fmt, ap);
  va_end(ap);
  return n;
}

int lc_appendf(char *buf, size_t buflen, int written, const char *fmt, ...) {
  if (written < 0) return written;
  if ((size_t)written >= buflen) return written;
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(buf + written, buflen - (size_t)written, fmt, ap);
  va_end(ap);
  return n < 0 ? n : written + n;
}

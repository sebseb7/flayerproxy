#ifndef MC_WIRE_H
#define MC_WIRE_H

#include <stddef.h>
#include <stdint.h>

#include "libchunk.h"

typedef struct mc_buf {
  uint8_t *data;
  size_t len;
  size_t cap;
} mc_buf;

void mc_buf_free(mc_buf *b);
lc_status mc_buf_reserve(mc_buf *b, size_t need);
lc_status mc_buf_write(mc_buf *b, const void *src, size_t n);
lc_status mc_buf_u8(mc_buf *b, uint8_t v);
lc_status mc_buf_i16_be(mc_buf *b, int16_t v);
lc_status mc_buf_i32_be(mc_buf *b, int32_t v);
lc_status mc_buf_i64_be(mc_buf *b, int64_t v);
lc_status mc_buf_f32_be(mc_buf *b, float v);
lc_status mc_buf_f64_be(mc_buf *b, double v);
lc_status mc_buf_varint(mc_buf *b, int32_t v);
lc_status mc_buf_string(mc_buf *b, const char *s);
lc_status mc_buf_uuid(mc_buf *b, const uint8_t uuid[16]);
lc_status mc_buf_block_pos(mc_buf *b, const lc_block_pos *p);

/** Frame: length varint + packet id varint + payload. */
lc_status mc_buf_frame(mc_buf *b, int32_t pkt_id, const uint8_t *payload, size_t payload_len);

#endif

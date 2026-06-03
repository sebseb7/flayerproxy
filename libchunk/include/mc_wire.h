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

/** Parsed Minecraft Java frame (length prefix + packet id + payload). */
typedef struct lc_frame_view {
  int32_t packet_id;
  const uint8_t *payload;
  size_t payload_len;
  size_t frame_bytes;
} lc_frame_view;

/**
 * Parse one frame from a receive buffer.
 * @return LC_OK full frame, LC_ERR_TRUNCATED need more bytes, LC_ERR_INVALID bad data
 */
lc_status lc_try_read_frame(const uint8_t *data, size_t len, lc_frame_view *out);

/** Build one frame into out (caller frees with lc_byte_buf_free). */
lc_status lc_build_frame(int32_t pkt_id, const uint8_t *payload, size_t payload_len, lc_byte_buf *out);

/** UTF-8 string as length-prefixed varint + bytes. */
lc_status lc_write_string(const char *s, lc_byte_buf *out);

/**
 * Read length-prefixed string at offset.
 * @param out_str malloc'd C string (caller must free); NULL on truncation
 * @param out_next byte offset after string
 */
lc_status lc_read_string_at(const uint8_t *data, size_t len, size_t offset, char **out_str, size_t *out_next);

/** Read one varint at offset; advances *offset on success. */
lc_status lc_read_varint_at(const uint8_t *data, size_t len, size_t *offset, int32_t *out);

/** Encode a single varint into out (caller frees with lc_byte_buf_free). */
lc_status lc_write_varint(int32_t value, lc_byte_buf *out);

/** Streaming reassembly of length-prefixed Minecraft frames. */
typedef struct lc_frame_reader {
  uint8_t *data;
  size_t len;
  size_t cap;
} lc_frame_reader;

typedef void (*lc_frame_callback)(void *ctx, int32_t packet_id, const uint8_t *payload,
                                  size_t payload_len);

void lc_frame_reader_init(lc_frame_reader *r);
void lc_frame_reader_reset(lc_frame_reader *r);
void lc_frame_reader_free(lc_frame_reader *r);

/**
 * Append chunk bytes and invoke cb for each complete frame.
 * @return LC_OK, LC_ERR_INVALID (oversize or bad frame), LC_ERR_OOM
 */
lc_status lc_frame_reader_feed(lc_frame_reader *r, const uint8_t *chunk, size_t chunk_len,
                               lc_frame_callback cb, void *ctx);

#endif

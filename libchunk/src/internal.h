#ifndef LIBCHUNK_INTERNAL_H
#define LIBCHUNK_INTERNAL_H

#include "libchunk.h"
#include <stdlib.h>
#include <string.h>

typedef struct lc_buf {
  const uint8_t *data;
  size_t len;
  size_t off;
} lc_buf;

typedef struct lc_bitfield_spec {
  int size;
  int signed_f;
} lc_bitfield_spec;

#define LC_META_END 0xff

static inline void lc_buf_init(lc_buf *b, const uint8_t *data, size_t len) {
  b->data = data;
  b->len = len;
  b->off = 0;
}

static inline size_t lc_buf_remaining(const lc_buf *b) {
  return b->len > b->off ? b->len - b->off : 0;
}

static inline lc_status lc_buf_need(const lc_buf *b, size_t n) {
  return lc_buf_remaining(b) >= n ? LC_OK : LC_ERR_TRUNCATED;
}

lc_status lc_buf_read_u8(lc_buf *b, uint8_t *out);
lc_status lc_buf_read_i8(lc_buf *b, int8_t *out);
lc_status lc_buf_read_u16_le(lc_buf *b, uint16_t *out);
lc_status lc_buf_read_u16_be(lc_buf *b, uint16_t *out);
lc_status lc_buf_read_u32_be(lc_buf *b, uint32_t *out);
lc_status lc_buf_read_i16_le(lc_buf *b, int16_t *out);
lc_status lc_buf_read_i16_be(lc_buf *b, int16_t *out);
lc_status lc_buf_read_i32_le(lc_buf *b, int32_t *out);
lc_status lc_buf_read_i32_be(lc_buf *b, int32_t *out);
lc_status lc_buf_read_u32_le(lc_buf *b, uint32_t *out);
lc_status lc_buf_read_i64_le(lc_buf *b, int64_t *out);
lc_status lc_buf_read_i64_be(lc_buf *b, int64_t *out);
lc_status lc_buf_read_f32_le(lc_buf *b, float *out);
lc_status lc_buf_read_f64_le(lc_buf *b, double *out);
lc_status lc_buf_read_bool(lc_buf *b, uint8_t *out);
lc_status lc_buf_read_varint(lc_buf *b, int32_t *out);
lc_status lc_buf_read_varlong(lc_buf *b, int64_t *out);
lc_status lc_buf_read_string(lc_buf *b, char **out);
lc_status lc_buf_read_byte_array(lc_buf *b, lc_byte_buf *out);
lc_status lc_buf_read_bytes(lc_buf *b, size_t n, lc_byte_buf *out);
lc_status lc_buf_read_uuid(lc_buf *b, lc_uuid *out);
lc_status lc_buf_read_position(lc_buf *b, lc_block_pos *out);
lc_status lc_buf_read_bitfield(lc_buf *b, const lc_bitfield_spec *fields, size_t nfields, int32_t *out_vals);
lc_status lc_buf_read_lpvec3(lc_buf *b, lc_vec3 *out);
lc_status lc_buf_read_i64_array(lc_buf *b, lc_i64_arr *out);
lc_status lc_buf_read_u8_array(lc_buf *b, lc_u8_arr *out);
lc_status lc_buf_read_u8_grid(lc_buf *b, lc_u8_grid *out);

lc_status lc_nbt_read_anonymous(lc_buf *b, lc_byte_buf *out);
lc_status lc_nbt_skip_anonymous(lc_buf *b);
lc_status lc_nbt_read_optional(lc_buf *b, lc_byte_buf *out, uint8_t *present);
lc_status lc_nbt_skip_anon_optional(lc_buf *b);
lc_status lc_nbt_read_anon_optional(lc_buf *b, lc_byte_buf *out, uint8_t *present);

int lc_nbt_fprint_wire(FILE *f, const char *indent, const uint8_t *data, size_t len);

int lc_wire_hex_fprint(FILE *f, const uint8_t *wire, size_t len);
int lc_registry_data_fprint(FILE *f, const lc_registry_data *p);

lc_status lc_slot_read(lc_buf *b, lc_equipment *eq);
lc_status lc_slot_skip(lc_buf *b);
lc_status lc_nbt_extract_chat_summary(lc_buf *b, char **text_out, char **translate_out);
int lc_decode_declare_commands(const uint8_t *payload, size_t payload_len, char *out, size_t out_sz);
int lc_decode_update_recipes(const uint8_t *payload, size_t payload_len, char *out, size_t out_sz);
int lc_decode_system_chat(const uint8_t *payload, size_t payload_len, char *out, size_t out_sz);
int lc_decode_set_ticking_state(const uint8_t *payload, size_t payload_len, char *out, size_t out_sz);
lc_status lc_read_top_bit_array(lc_buf *b, lc_equipment_arr *out);
const char *lc_slot_component_type_name(int32_t id);
int lc_slot_fprint_equipment_entry(FILE *f, const lc_equipment *eq, const char *indent);
lc_status lc_metadata_read_loop(lc_buf *b, lc_metadata_arr *out);
lc_status lc_parse_spawn_info(lc_buf *b, lc_spawn_info *out);

const char *lc_heightmap_type_name(int id);
const char *lc_metadata_type_name(int id);

lc_status lc_write_rgb_png(const char *path, const uint8_t *rgb, int w, int h);
lc_status lc_read_rgb_png(const char *path, uint8_t **rgb, int *w, int *h);
lc_status lc_write_rgb_avif(const char *path, const uint8_t *rgb, int w, int h);
lc_status lc_read_rgb_avif(const char *path, uint8_t **rgb, int *w, int *h);

int lc_snprintf(char *buf, size_t buflen, const char *fmt, ...);
int lc_appendf(char *buf, size_t buflen, int written, const char *fmt, ...);

#endif

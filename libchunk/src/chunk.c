#include "internal.h"

#define LC_CHUNK_MAGIC 0x4b43434c /* "LCCK" little-endian */
#define LC_CHUNK_FMT_VERSION 1
#define LC_MAX_BITS_PER_BLOCK 8
#define LC_BIOME_VOLUME 64
/** Local biome palette (bits 1–3); values above use direct global biome ids in data. */
#define LC_MAX_BITS_PER_BIOME 3
/** 1.21.5+ chunkData paletted containers omit long-count varint and single-value pad byte. */
#define LC_MAP_CHUNK_NO_SIZE_PREFIX 1

/* --- growable output buffer --- */

typedef struct lc_out_buf {
  uint8_t *data;
  size_t len;
  size_t cap;
} lc_out_buf;

static lc_status lc_out_reserve(lc_out_buf *o, size_t need) {
  if (need <= o->cap) return LC_OK;
  size_t ncap = o->cap ? o->cap : 64;
  while (ncap < need) {
    if (ncap > SIZE_MAX / 2) return LC_ERR_OOM;
    ncap *= 2;
  }
  uint8_t *p = (uint8_t *)realloc(o->data, ncap);
  if (!p) return LC_ERR_OOM;
  o->data = p;
  o->cap = ncap;
  return LC_OK;
}

static lc_status lc_out_write(lc_out_buf *o, const void *src, size_t n) {
  if (lc_out_reserve(o, o->len + n) != LC_OK) return LC_ERR_OOM;
  memcpy(o->data + o->len, src, n);
  o->len += n;
  return LC_OK;
}

static lc_status lc_out_u8(lc_out_buf *o, uint8_t v) { return lc_out_write(o, &v, 1); }

static lc_status lc_out_i16_be(lc_out_buf *o, int16_t v) {
  uint8_t b[2] = {(uint8_t)((uint16_t)v >> 8), (uint8_t)v};
  return lc_out_write(o, b, 2);
}

static lc_status lc_out_i16_le(lc_out_buf *o, int16_t v) {
  uint8_t b[2] = {(uint8_t)v, (uint8_t)((uint16_t)v >> 8)};
  return lc_out_write(o, b, 2);
}

static lc_status lc_out_i32_le(lc_out_buf *o, int32_t v) {
  uint8_t b[4];
  b[0] = (uint8_t)v;
  b[1] = (uint8_t)(v >> 8);
  b[2] = (uint8_t)(v >> 16);
  b[3] = (uint8_t)(v >> 24);
  return lc_out_write(o, b, 4);
}

static lc_status lc_out_u32_be(lc_out_buf *o, uint32_t v) {
  uint8_t b[4] = {(uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v};
  return lc_out_write(o, b, 4);
}

static lc_status lc_out_i64_le(lc_out_buf *o, int64_t v) {
  uint8_t b[8];
  for (int i = 0; i < 8; i++) b[i] = (uint8_t)(v >> (8 * i));
  return lc_out_write(o, b, 8);
}

static lc_status lc_out_varint(lc_out_buf *o, int32_t value) {
  uint32_t v = (uint32_t)value;
  do {
    uint8_t temp = (uint8_t)(v & 0x7f);
    v >>= 7;
    if (v) temp |= 0x80;
    if (lc_out_u8(o, temp) != LC_OK) return LC_ERR_OOM;
  } while (v);
  return LC_OK;
}

static void lc_out_free(lc_out_buf *o) {
  free(o->data);
  o->data = NULL;
  o->len = o->cap = 0;
}

/* --- section helpers --- */

static int lc_block_index(int lx, int ly, int lz) { return (ly << 8) | (lz << 4) | lx; }

static int32_t lc_section_y_for_block(int32_t world_y, int32_t min_y) {
  (void)min_y;
  return world_y >> 4;
}

static int lc_paletted_long_count(int bits_per_value, int capacity) {
  if (bits_per_value <= 0 || bits_per_value > 32) return 0;
  int values_per_long = 64 / bits_per_value;
  if (values_per_long <= 0) return 0;
  return (capacity + values_per_long - 1) / values_per_long;
}

static lc_chunk_section *lc_chunk_find_section(lc_chunk *c, int32_t section_y) {
  for (size_t i = 0; i < c->section_count; i++) {
    if (c->sections[i].section_y == section_y) return &c->sections[i];
  }
  return NULL;
}

static lc_status lc_chunk_add_section(lc_chunk *c, int32_t section_y, lc_chunk_section **out) {
  lc_chunk_section *next =
      (lc_chunk_section *)realloc(c->sections, (c->section_count + 1) * sizeof(lc_chunk_section));
  if (!next) return LC_ERR_OOM;
  c->sections = next;
  lc_chunk_section *sec = &c->sections[c->section_count++];
  memset(sec, 0, sizeof(*sec));
  sec->section_y = section_y;
  *out = sec;
  return LC_OK;
}

static lc_status lc_chunk_get_or_create_section(lc_chunk *c, int32_t section_y, lc_chunk_section **out) {
  lc_chunk_section *sec = lc_chunk_find_section(c, section_y);
  if (sec) {
    *out = sec;
    return LC_OK;
  }
  return lc_chunk_add_section(c, section_y, out);
}

static int16_t lc_count_solid_blocks(const int32_t *state_ids) {
  int16_t count = 0;
  for (int i = 0; i < LC_BLOCK_VOLUME; i++) {
    if (state_ids[i] != 0) count++;
  }
  return count;
}

/* --- wire BitArray (Minecraft PalettedContainer: BE u32 pairs per 64-bit long) --- */

typedef struct lc_bitarray {
  uint32_t *data;
  size_t data_len; /* uint32 count */
  int capacity;
  int bits_per_value;
  int values_per_long;
  uint32_t value_mask;
} lc_bitarray;

static void lc_bitarray_free(lc_bitarray *ba) {
  free(ba->data);
  memset(ba, 0, sizeof(*ba));
}

static lc_status lc_bitarray_init(lc_bitarray *ba, int bits_per_value, int capacity) {
  int values_per_long = 64 / bits_per_value;
  size_t buffer_size = (size_t)((capacity + values_per_long - 1) / values_per_long) * 2;
  ba->data = (uint32_t *)calloc(buffer_size, sizeof(uint32_t));
  if (!ba->data) return LC_ERR_OOM;
  ba->data_len = buffer_size;
  ba->capacity = capacity;
  ba->bits_per_value = bits_per_value;
  ba->values_per_long = values_per_long;
  ba->value_mask = (1u << bits_per_value) - 1u;
  return LC_OK;
}

static uint32_t lc_bitarray_get(const lc_bitarray *ba, int index) {
  int start_long_index = index / ba->values_per_long;
  int index_in_long = (index - start_long_index * ba->values_per_long) * ba->bits_per_value;
  if (index_in_long >= 32) {
    int index_in_start_long = index_in_long - 32;
    uint32_t start_long = ba->data[start_long_index * 2 + 1];
    return (start_long >> index_in_start_long) & ba->value_mask;
  }
  uint32_t start_long = ba->data[start_long_index * 2];
  int index_in_start_long = index_in_long;
  uint32_t result = start_long >> index_in_start_long;
  int end_bit_offset = index_in_start_long + ba->bits_per_value;
  if (end_bit_offset > 32) {
    uint32_t end_long = ba->data[start_long_index * 2 + 1];
    result |= end_long << (32 - index_in_start_long);
  }
  return result & ba->value_mask;
}

static void lc_bitarray_set(lc_bitarray *ba, int index, uint32_t value) {
  value &= ba->value_mask;
  int start_long_index = index / ba->values_per_long;
  int index_in_long = (index - start_long_index * ba->values_per_long) * ba->bits_per_value;
  if (index_in_long >= 32) {
    int index_in_start_long = index_in_long - 32;
    ba->data[start_long_index * 2 + 1] =
        (ba->data[start_long_index * 2 + 1] & ~(ba->value_mask << index_in_start_long)) |
        (value << index_in_start_long);
    return;
  }
  int index_in_start_long = index_in_long;
  ba->data[start_long_index * 2] =
      (ba->data[start_long_index * 2] & ~(ba->value_mask << index_in_start_long)) |
      (value << index_in_start_long);
  int end_bit_offset = index_in_start_long + ba->bits_per_value;
  if (end_bit_offset > 32) {
    ba->data[start_long_index * 2 + 1] =
        (ba->data[start_long_index * 2 + 1] & ~((1u << (end_bit_offset - 32)) - 1u)) |
        (value >> (32 - index_in_start_long));
  }
}

static lc_status lc_bitarray_read(lc_bitarray *ba, lc_buf *b, int long_count) {
  size_t need = (size_t)long_count * 2;
  if (need > ba->data_len) {
    uint32_t *p = (uint32_t *)realloc(ba->data, need * sizeof(uint32_t));
    if (!p) return LC_ERR_OOM;
    ba->data = p;
    ba->data_len = need;
  }
  for (int i = 0; i < long_count * 2; i += 2) {
    uint32_t hi, lo;
    if (lc_buf_read_u32_be(b, &hi) != LC_OK) return LC_ERR_TRUNCATED;
    if (lc_buf_read_u32_be(b, &lo) != LC_OK) return LC_ERR_TRUNCATED;
    ba->data[i + 1] = hi;
    ba->data[i] = lo;
  }
  return LC_OK;
}

static lc_status lc_bitarray_write(const lc_bitarray *ba, lc_out_buf *o) {
  for (size_t i = 0; i + 1 < ba->data_len; i += 2) {
    if (lc_out_u32_be(o, ba->data[i + 1]) != LC_OK) return LC_ERR_OOM;
    if (lc_out_u32_be(o, ba->data[i]) != LC_OK) return LC_ERR_OOM;
  }
  return LC_OK;
}

/* --- chunk_data decode / encode --- */

static lc_status lc_skip_paletted_data(lc_buf *b, int bits_per_value, int capacity) {
  int long_count;
  if (LC_MAP_CHUNK_NO_SIZE_PREFIX) {
    long_count = lc_paletted_long_count(bits_per_value, capacity);
  } else {
    int32_t wire;
    if (lc_buf_read_varint(b, &wire) != LC_OK) return LC_ERR_TRUNCATED;
    if (wire < 0) return LC_ERR_INVALID;
    int expected = lc_paletted_long_count(bits_per_value, capacity);
    long_count = wire > 0 ? (int)wire : expected;
    if (expected > 0 && long_count < expected) long_count = expected;
  }
  if (long_count < 0) return LC_ERR_INVALID;
  if (lc_buf_need(b, (size_t)long_count * 8) != LC_OK) return LC_ERR_TRUNCATED;
  b->off += (size_t)long_count * 8;
  return LC_OK;
}

static lc_status lc_decode_biome_section(lc_buf *b, lc_chunk_section *sec) {
  uint8_t bits_per_biome;
  if (lc_buf_read_u8(b, &bits_per_biome) != LC_OK) return LC_ERR_TRUNCATED;
  if (bits_per_biome > 8) return LC_ERR_INVALID;

  if (bits_per_biome == 0) {
    int32_t biome_id;
    if (lc_buf_read_varint(b, &biome_id) != LC_OK) return LC_ERR_TRUNCATED;
    if (!LC_MAP_CHUNK_NO_SIZE_PREFIX) {
      uint8_t pad;
      if (lc_buf_read_u8(b, &pad) != LC_OK) return LC_ERR_TRUNCATED;
    }
    for (int i = 0; i < LC_BIOME_VOLUME; i++) sec->biome_ids[i] = biome_id;
    sec->has_biomes = 1;
    return LC_OK;
  }

  int32_t palette[LC_BIOME_VOLUME];
  int palette_len = 0;

  /* Direct global palette (prismarine-chunk: bits > MAX_BITS_PER_BIOME). */
  if (bits_per_biome > LC_MAX_BITS_PER_BIOME) {
    int bits_per_value = (int)bits_per_biome;
    int long_count;
    if (LC_MAP_CHUNK_NO_SIZE_PREFIX) {
      long_count = lc_paletted_long_count(bits_per_value, LC_BIOME_VOLUME);
    } else {
      int32_t wire;
      if (lc_buf_read_varint(b, &wire) != LC_OK) return LC_ERR_TRUNCATED;
      if (wire < 0) return LC_ERR_INVALID;
      int expected = lc_paletted_long_count(bits_per_value, LC_BIOME_VOLUME);
      long_count = wire > 0 ? (int)wire : expected;
      if (expected > 0 && long_count < expected) long_count = expected;
    }
    if (long_count <= 0) return LC_ERR_INVALID;

    lc_bitarray ba;
    if (lc_bitarray_init(&ba, bits_per_value, LC_BIOME_VOLUME) != LC_OK) return LC_ERR_OOM;
    lc_status st = lc_bitarray_read(&ba, b, long_count);
    if (st != LC_OK) {
      lc_bitarray_free(&ba);
      return st;
    }
    for (int i = 0; i < LC_BIOME_VOLUME; i++) sec->biome_ids[i] = (int32_t)lc_bitarray_get(&ba, i);
    lc_bitarray_free(&ba);
    sec->has_biomes = 1;
    return LC_OK;
  }

  if (lc_buf_read_varint(b, &palette_len) != LC_OK) return LC_ERR_TRUNCATED;
  if (palette_len < 0 || palette_len > LC_BIOME_VOLUME) return LC_ERR_INVALID;
  for (int32_t i = 0; i < palette_len; i++) {
    if (lc_buf_read_varint(b, &palette[i]) != LC_OK) return LC_ERR_TRUNCATED;
  }

  int bits_per_value = (int)bits_per_biome;
  int long_count;
  if (LC_MAP_CHUNK_NO_SIZE_PREFIX) {
    long_count = lc_paletted_long_count(bits_per_value, LC_BIOME_VOLUME);
  } else {
    int32_t wire;
    if (lc_buf_read_varint(b, &wire) != LC_OK) return LC_ERR_TRUNCATED;
    if (wire < 0) return LC_ERR_INVALID;
    int expected = lc_paletted_long_count(bits_per_value, LC_BIOME_VOLUME);
    long_count = wire > 0 ? (int)wire : expected;
    if (expected > 0 && long_count < expected) long_count = expected;
  }
  if (long_count <= 0) return LC_ERR_INVALID;

  lc_bitarray ba;
  if (lc_bitarray_init(&ba, bits_per_value, LC_BIOME_VOLUME) != LC_OK) return LC_ERR_OOM;
  lc_status st = lc_bitarray_read(&ba, b, long_count);
  if (st != LC_OK) {
    lc_bitarray_free(&ba);
    return st;
  }

  for (int i = 0; i < LC_BIOME_VOLUME; i++) {
    uint32_t idx = lc_bitarray_get(&ba, i);
    if (palette_len > 0) {
      if (idx >= (uint32_t)palette_len) {
        lc_bitarray_free(&ba);
        return LC_ERR_INVALID;
      }
      sec->biome_ids[i] = palette[idx];
    } else {
      sec->biome_ids[i] = (int32_t)idx;
    }
  }
  lc_bitarray_free(&ba);
  sec->has_biomes = 1;
  return LC_OK;
}

static lc_status lc_decode_section(lc_buf *b, int32_t section_y, lc_chunk_section *sec) {
  int16_t solid;
  if (lc_buf_read_i16_be(b, &solid) != LC_OK) return LC_ERR_TRUNCATED;
  uint8_t bits_per_block;
  if (lc_buf_read_u8(b, &bits_per_block) != LC_OK) return LC_ERR_TRUNCATED;

  int32_t palette[4096];
  int palette_len = 0;

  sec->section_y = section_y;
  sec->solid_block_count = solid;

  if (bits_per_block == 0) {
    int32_t state;
    if (lc_buf_read_varint(b, &state) != LC_OK) return LC_ERR_TRUNCATED;
    if (!LC_MAP_CHUNK_NO_SIZE_PREFIX) {
      uint8_t pad;
      if (lc_buf_read_u8(b, &pad) != LC_OK) return LC_ERR_TRUNCATED;
    }
    for (int i = 0; i < LC_BLOCK_VOLUME; i++) sec->state_ids[i] = state;
    return LC_OK;
  }

  if (bits_per_block <= LC_MAX_BITS_PER_BLOCK) {
    int32_t num_palette;
    if (lc_buf_read_varint(b, &num_palette) != LC_OK) return LC_ERR_TRUNCATED;
    if (num_palette < 0 || num_palette > 4096) return LC_ERR_INVALID;
    palette_len = num_palette;
    for (int32_t i = 0; i < num_palette; i++) {
      if (lc_buf_read_varint(b, &palette[i]) != LC_OK) return LC_ERR_TRUNCATED;
    }
  }

  int bits_per_value;
  if (bits_per_block > LC_MAX_BITS_PER_BLOCK) {
    bits_per_value = (int)bits_per_block;
    if (bits_per_value > 31) return LC_ERR_INVALID;
  } else {
    bits_per_value = (int)bits_per_block;
  }

  int long_count;
  if (LC_MAP_CHUNK_NO_SIZE_PREFIX) {
    long_count = lc_paletted_long_count(bits_per_value, LC_BLOCK_VOLUME);
  } else {
    int32_t wire;
    if (lc_buf_read_varint(b, &wire) != LC_OK) return LC_ERR_TRUNCATED;
    if (wire < 0) return LC_ERR_INVALID;
    int expected = lc_paletted_long_count(bits_per_value, LC_BLOCK_VOLUME);
    long_count = wire > 0 ? (int)wire : expected;
    if (expected > 0 && long_count < expected) long_count = expected;
  }
  if (long_count <= 0) return LC_ERR_INVALID;

  lc_bitarray ba;
  if (lc_bitarray_init(&ba, bits_per_value, LC_BLOCK_VOLUME) != LC_OK) return LC_ERR_OOM;
  lc_status st = lc_bitarray_read(&ba, b, long_count);
  if (st != LC_OK) {
    lc_bitarray_free(&ba);
    return st;
  }

  for (int i = 0; i < LC_BLOCK_VOLUME; i++) {
    uint32_t idx = lc_bitarray_get(&ba, i);
    if (palette_len > 0) {
      if (idx >= (uint32_t)palette_len) {
        lc_bitarray_free(&ba);
        return LC_ERR_INVALID;
      }
      sec->state_ids[i] = palette[idx];
    } else {
      sec->state_ids[i] = (int32_t)idx;
    }
  }
  lc_bitarray_free(&ba);
  return LC_OK;
}

static lc_status lc_decode_chunk_data(const lc_byte_buf *data, int32_t min_y, int32_t world_height,
                                      lc_chunk *out) {
  if (!data->len) return LC_OK;
  lc_buf b;
  lc_buf_init(&b, data->data, data->len);
  int num_sections = world_height >> 4;
  int32_t base_section_y = min_y >> 4;

  for (int i = 0; i < num_sections; i++) {
    if (lc_buf_remaining(&b) == 0) break;
    lc_chunk_section *sec = NULL;
    if (lc_chunk_add_section(out, base_section_y + i, &sec) != LC_OK) return LC_ERR_OOM;
    lc_status st = lc_decode_section(&b, sec->section_y, sec);
    if (st != LC_OK) {
      out->section_count--;
      break;
    }
    st = lc_decode_biome_section(&b, sec);
    if (st != LC_OK) {
      out->section_count--;
      break;
    }
  }
  return LC_OK;
}

static int lc_bits_for_palette(size_t palette_len) {
  if (palette_len <= 1) return 0;
  int bits = 0;
  size_t n = palette_len - 1;
  while (n) {
    bits++;
    n >>= 1;
  }
  if (bits >= 1 && bits <= 3) bits = 4;
  return bits;
}

typedef struct lc_palette_build {
  int32_t entries[4096];
  size_t count;
  uint16_t indices[LC_BLOCK_VOLUME];
} lc_palette_build;

static void lc_build_palette(const int32_t *state_ids, lc_palette_build *pal) {
  pal->count = 0;
  for (int i = 0; i < LC_BLOCK_VOLUME; i++) {
    int32_t sid = state_ids[i];
    size_t j;
    for (j = 0; j < pal->count; j++) {
      if (pal->entries[j] == sid) break;
    }
    if (j == pal->count) pal->entries[pal->count++] = sid;
    pal->indices[i] = (uint16_t)j;
  }
}

static lc_status lc_encode_section(const lc_chunk_section *sec, lc_out_buf *o) {
  lc_palette_build pal;
  lc_build_palette(sec->state_ids, &pal);
  int bits = lc_bits_for_palette(pal.count);
  int16_t solid = lc_count_solid_blocks(sec->state_ids);

  if (lc_out_i16_be(o, solid) != LC_OK) return LC_ERR_OOM;
  if (lc_out_u8(o, (uint8_t)bits) != LC_OK) return LC_ERR_OOM;

  if (bits <= LC_MAX_BITS_PER_BLOCK) {
    if (lc_out_varint(o, (int32_t)pal.count) != LC_OK) return LC_ERR_OOM;
    for (size_t i = 0; i < pal.count; i++) {
      if (lc_out_varint(o, pal.entries[i]) != LC_OK) return LC_ERR_OOM;
    }
  }

  if (bits == 0) {
    if (pal.count != 1) return LC_ERR_INVALID;
    if (lc_out_varint(o, pal.entries[0]) != LC_OK) return LC_ERR_OOM;
    if (!LC_MAP_CHUNK_NO_SIZE_PREFIX) {
      if (lc_out_u8(o, 0) != LC_OK) return LC_ERR_OOM;
    }
    return LC_OK;
  }

  int bits_per_value = bits;
  if (bits_per_value > LC_MAX_BITS_PER_BLOCK && bits_per_value > 31) return LC_ERR_INVALID;
  lc_bitarray ba;
  if (lc_bitarray_init(&ba, bits_per_value, LC_BLOCK_VOLUME) != LC_OK) return LC_ERR_OOM;

  if (bits <= LC_MAX_BITS_PER_BLOCK) {
    for (int i = 0; i < LC_BLOCK_VOLUME; i++) lc_bitarray_set(&ba, i, pal.indices[i]);
  } else {
    for (int i = 0; i < LC_BLOCK_VOLUME; i++) lc_bitarray_set(&ba, i, (uint32_t)sec->state_ids[i]);
  }

  int long_count = lc_paletted_long_count(bits_per_value, LC_BLOCK_VOLUME);
  if (!LC_MAP_CHUNK_NO_SIZE_PREFIX) {
    if (lc_out_varint(o, long_count) != LC_OK) {
      lc_bitarray_free(&ba);
      return LC_ERR_OOM;
    }
  }
  lc_status st = lc_bitarray_write(&ba, o);
  lc_bitarray_free(&ba);
  return st;
}

static int lc_chunk_compare_section_y(const void *a, const void *b) {
  const lc_chunk_section *sa = (const lc_chunk_section *)a;
  const lc_chunk_section *sb = (const lc_chunk_section *)b;
  if (sa->section_y < sb->section_y) return -1;
  if (sa->section_y > sb->section_y) return 1;
  return 0;
}

static lc_status lc_encode_chunk_data(const lc_chunk *c, lc_byte_buf *out) {
  lc_out_buf o;
  memset(&o, 0, sizeof(o));
  if (c->section_count == 0) {
    out->data = NULL;
    out->len = 0;
    return LC_OK;
  }

  lc_chunk_section *sorted = (lc_chunk_section *)malloc(c->section_count * sizeof(lc_chunk_section));
  if (!sorted) return LC_ERR_OOM;
  memcpy(sorted, c->sections, c->section_count * sizeof(lc_chunk_section));
  qsort(sorted, c->section_count, sizeof(lc_chunk_section), lc_chunk_compare_section_y);

  int32_t first_y = sorted[0].section_y;
  int32_t last_y = sorted[c->section_count - 1].section_y;
  int32_t base_y = c->min_y >> 4;
  if (first_y < base_y) first_y = base_y;

  lc_status st = LC_OK;
  for (int32_t sy = first_y; sy <= last_y; sy++) {
    lc_chunk_section *match = NULL;
    for (size_t i = 0; i < c->section_count; i++) {
      if (c->sections[i].section_y == sy) {
        match = &c->sections[i];
        break;
      }
    }
    lc_chunk_section air;
    if (!match) {
      memset(&air, 0, sizeof(air));
      air.section_y = sy;
      match = &air;
    }
    st = lc_encode_section(match, &o);
    if (st != LC_OK) break;
  }

  free(sorted);
  if (st != LC_OK) {
    lc_out_free(&o);
    return st;
  }
  out->data = o.data;
  out->len = o.len;
  return LC_OK;
}

/* --- deep copy helpers --- */

static lc_status lc_copy_i64_arr(const lc_i64_arr *src, lc_i64_arr *dst) {
  memset(dst, 0, sizeof(*dst));
  if (!src->count) return LC_OK;
  dst->values = (int64_t *)malloc(src->count * sizeof(int64_t));
  if (!dst->values) return LC_ERR_OOM;
  memcpy(dst->values, src->values, src->count * sizeof(int64_t));
  dst->count = src->count;
  return LC_OK;
}

static lc_status lc_copy_u8_grid(const lc_u8_grid *src, lc_u8_grid *dst) {
  memset(dst, 0, sizeof(*dst));
  if (!src->row_count) return LC_OK;
  dst->row_count = src->row_count;
  dst->rows = (uint8_t **)calloc(src->row_count, sizeof(uint8_t *));
  dst->row_lens = (size_t *)calloc(src->row_count, sizeof(size_t));
  if (!dst->rows || !dst->row_lens) {
    lc_u8_grid_free(dst);
    return LC_ERR_OOM;
  }
  for (size_t i = 0; i < src->row_count; i++) {
    if (src->row_lens[i]) {
      dst->rows[i] = (uint8_t *)malloc(src->row_lens[i]);
      if (!dst->rows[i]) {
        lc_u8_grid_free(dst);
        return LC_ERR_OOM;
      }
      memcpy(dst->rows[i], src->rows[i], src->row_lens[i]);
    }
    dst->row_lens[i] = src->row_lens[i];
  }
  return LC_OK;
}

static lc_status lc_copy_byte_buf(const lc_byte_buf *src, lc_byte_buf *dst) {
  memset(dst, 0, sizeof(*dst));
  if (!src->len) return LC_OK;
  dst->data = (uint8_t *)malloc(src->len);
  if (!dst->data) return LC_ERR_OOM;
  memcpy(dst->data, src->data, src->len);
  dst->len = src->len;
  return LC_OK;
}

static lc_status lc_copy_heightmap_arr(const lc_heightmap_arr *src, lc_heightmap_arr *dst) {
  memset(dst, 0, sizeof(*dst));
  if (!src->count) return LC_OK;
  dst->items = (lc_heightmap *)calloc(src->count, sizeof(lc_heightmap));
  if (!dst->items) return LC_ERR_OOM;
  dst->count = src->count;
  for (size_t i = 0; i < src->count; i++) {
    dst->items[i].type_id = src->items[i].type_id;
    dst->items[i].type_name = src->items[i].type_name;
    if (lc_copy_i64_arr(&src->items[i].data, &dst->items[i].data) != LC_OK) {
      lc_heightmap_arr_free(dst);
      return LC_ERR_OOM;
    }
  }
  return LC_OK;
}

static lc_status lc_copy_block_entity_arr(const lc_block_entity_arr *src, lc_block_entity_arr *dst) {
  memset(dst, 0, sizeof(*dst));
  if (!src->count) return LC_OK;
  dst->items = (lc_block_entity *)calloc(src->count, sizeof(lc_block_entity));
  if (!dst->items) return LC_ERR_OOM;
  dst->count = src->count;
  for (size_t i = 0; i < src->count; i++) {
    dst->items[i].x = src->items[i].x;
    dst->items[i].z = src->items[i].z;
    dst->items[i].y = src->items[i].y;
    dst->items[i].type = src->items[i].type;
    if (lc_copy_byte_buf(&src->items[i].nbt, &dst->items[i].nbt) != LC_OK) {
      lc_block_entity_arr_free(dst);
      return LC_ERR_OOM;
    }
  }
  return LC_OK;
}

static lc_status lc_write_heightmaps(lc_out_buf *o, const lc_heightmap_arr *hm) {
  if (lc_out_varint(o, (int32_t)hm->count) != LC_OK) return LC_ERR_OOM;
  for (size_t i = 0; i < hm->count; i++) {
    if (lc_out_varint(o, hm->items[i].type_id) != LC_OK) return LC_ERR_OOM;
    if (lc_out_varint(o, (int32_t)hm->items[i].data.count) != LC_OK) return LC_ERR_OOM;
    for (size_t j = 0; j < hm->items[i].data.count; j++) {
      if (lc_out_i64_le(o, hm->items[i].data.values[j]) != LC_OK) return LC_ERR_OOM;
    }
  }
  return LC_OK;
}

static lc_status lc_read_heightmaps(lc_buf *b, lc_heightmap_arr *out) {
  int32_t count;
  if (lc_buf_read_varint(b, &count) != LC_OK) return LC_ERR_TRUNCATED;
  if (count < 0) return LC_ERR_INVALID;
  out->count = (size_t)count;
  out->items = count ? (lc_heightmap *)calloc((size_t)count, sizeof(lc_heightmap)) : NULL;
  if (count && !out->items) return LC_ERR_OOM;
  for (int32_t i = 0; i < count; i++) {
    int32_t type_id;
    if (lc_buf_read_varint(b, &type_id) != LC_OK) goto fail;
    out->items[i].type_id = type_id;
    out->items[i].type_name = lc_heightmap_type_name(type_id);
    if (lc_buf_read_i64_array(b, &out->items[i].data) != LC_OK) goto fail;
  }
  return LC_OK;
fail:
  lc_heightmap_arr_free(out);
  return LC_ERR_TRUNCATED;
}

static lc_status lc_write_block_entities(lc_out_buf *o, const lc_block_entity_arr *be) {
  if (lc_out_varint(o, (int32_t)be->count) != LC_OK) return LC_ERR_OOM;
  for (size_t i = 0; i < be->count; i++) {
    const lc_block_entity *e = &be->items[i];
    uint8_t xz = (uint8_t)(((e->x & 0x0f) << 4) | (e->z & 0x0f));
    if (lc_out_u8(o, xz) != LC_OK) return LC_ERR_OOM;
    if (lc_out_i16_le(o, e->y) != LC_OK) return LC_ERR_OOM;
    if (lc_out_varint(o, e->type) != LC_OK) return LC_ERR_OOM;
    uint8_t present = e->nbt.len ? 1 : 0;
    if (lc_out_u8(o, present) != LC_OK) return LC_ERR_OOM;
    if (present) {
      if (lc_out_varint(o, (int32_t)e->nbt.len) != LC_OK) return LC_ERR_OOM;
      if (lc_out_write(o, e->nbt.data, e->nbt.len) != LC_OK) return LC_ERR_OOM;
    }
  }
  return LC_OK;
}

static lc_status lc_read_block_entities(lc_buf *b, lc_block_entity_arr *out) {
  int32_t count;
  if (lc_buf_read_varint(b, &count) != LC_OK) return LC_ERR_TRUNCATED;
  if (count < 0) return LC_ERR_INVALID;
  out->count = (size_t)count;
  out->items = count ? (lc_block_entity *)calloc((size_t)count, sizeof(lc_block_entity)) : NULL;
  if (count && !out->items) return LC_ERR_OOM;
  for (int32_t i = 0; i < count; i++) {
    uint8_t xz;
    if (lc_buf_read_u8(b, &xz) != LC_OK) goto fail;
    out->items[i].x = (xz >> 4) & 0x0f;
    out->items[i].z = xz & 0x0f;
    if (lc_buf_read_i16_le(b, &out->items[i].y) != LC_OK) goto fail;
    if (lc_buf_read_varint(b, &out->items[i].type) != LC_OK) goto fail;
    uint8_t present;
    if (lc_buf_read_u8(b, &present) != LC_OK) goto fail;
    if (present) {
      if (lc_buf_read_byte_array(b, &out->items[i].nbt) != LC_OK) goto fail;
    } else {
      out->items[i].nbt.data = NULL;
      out->items[i].nbt.len = 0;
    }
  }
  return LC_OK;
fail:
  lc_block_entity_arr_free(out);
  return LC_ERR_TRUNCATED;
}

static lc_status lc_write_i64_array(lc_out_buf *o, const lc_i64_arr *a) {
  if (lc_out_varint(o, (int32_t)a->count) != LC_OK) return LC_ERR_OOM;
  for (size_t i = 0; i < a->count; i++) {
    if (lc_out_i64_le(o, a->values[i]) != LC_OK) return LC_ERR_OOM;
  }
  return LC_OK;
}

static lc_status lc_write_u8_grid(lc_out_buf *o, const lc_u8_grid *g) {
  if (lc_out_varint(o, (int32_t)g->row_count) != LC_OK) return LC_ERR_OOM;
  for (size_t i = 0; i < g->row_count; i++) {
    if (lc_out_varint(o, (int32_t)g->row_lens[i]) != LC_OK) return LC_ERR_OOM;
    if (g->row_lens[i]) {
      if (lc_out_write(o, g->rows[i], g->row_lens[i]) != LC_OK) return LC_ERR_OOM;
    }
  }
  return LC_OK;
}

/* --- public API --- */

void lc_chunk_init(lc_chunk *c) {
  memset(c, 0, sizeof(*c));
  c->min_y = LC_CHUNK_DEFAULT_MIN_Y;
  c->world_height = LC_CHUNK_DEFAULT_WORLD_HEIGHT;
}

#define LC_PLAINS_BIOME_ID_DEFAULT 40

int32_t lc_chunk_biome_at(const lc_chunk *c, int lx, int32_t world_y, int lz) {
  if (!c) return LC_PLAINS_BIOME_ID_DEFAULT;
  int32_t sec_y = world_y >> 4;
  const lc_chunk_section *sec = NULL;
  for (size_t i = 0; i < c->section_count; i++) {
    if (c->sections[i].section_y == sec_y) {
      sec = &c->sections[i];
      break;
    }
  }
  if (!sec || !sec->has_biomes) return LC_PLAINS_BIOME_ID_DEFAULT;
  int ly = world_y & 15;
  if (lx < 0 || lx > 15 || lz < 0 || lz > 15) return LC_PLAINS_BIOME_ID_DEFAULT;
  int idx = (lx >> 2) + ((lz >> 2) << 2) + ((ly >> 2) << 4);
  return sec->biome_ids[idx];
}

void lc_chunk_free(lc_chunk *c) {
  if (!c) return;
  free(c->sections);
  lc_heightmap_arr_free(&c->heightmaps);
  lc_block_entity_arr_free(&c->block_entities);
  lc_i64_arr_free(&c->sky_light_mask);
  lc_i64_arr_free(&c->block_light_mask);
  lc_i64_arr_free(&c->empty_sky_light_mask);
  lc_i64_arr_free(&c->empty_block_light_mask);
  lc_u8_grid_free(&c->sky_light);
  lc_u8_grid_free(&c->block_light);
  memset(c, 0, sizeof(*c));
}

lc_status lc_chunk_from_map_chunk(const lc_map_chunk *mc, lc_chunk *out) {
  if (!mc || !out) return LC_ERR_INVALID;
  memset(out, 0, sizeof(*out));
  lc_chunk_init(out);
  out->x = mc->x;
  out->z = mc->z;

  lc_status st = lc_decode_chunk_data(&mc->chunk_data, out->min_y, out->world_height, out);
  if (st != LC_OK) {
    lc_chunk_free(out);
    return st;
  }

  st = lc_copy_heightmap_arr(&mc->heightmaps, &out->heightmaps);
  if (st != LC_OK) goto fail;
  st = lc_copy_block_entity_arr(&mc->block_entities, &out->block_entities);
  if (st != LC_OK) goto fail;
  st = lc_copy_i64_arr(&mc->sky_light_mask, &out->sky_light_mask);
  if (st != LC_OK) goto fail;
  st = lc_copy_i64_arr(&mc->block_light_mask, &out->block_light_mask);
  if (st != LC_OK) goto fail;
  st = lc_copy_i64_arr(&mc->empty_sky_light_mask, &out->empty_sky_light_mask);
  if (st != LC_OK) goto fail;
  st = lc_copy_i64_arr(&mc->empty_block_light_mask, &out->empty_block_light_mask);
  if (st != LC_OK) goto fail;
  st = lc_copy_u8_grid(&mc->sky_light, &out->sky_light);
  if (st != LC_OK) goto fail;
  st = lc_copy_u8_grid(&mc->block_light, &out->block_light);
  if (st != LC_OK) goto fail;
  return LC_OK;
fail:
  lc_chunk_free(out);
  return st;
}

lc_status lc_chunk_apply_update_light(lc_chunk *c, const lc_update_light *ul) {
  if (!c || !ul) return LC_ERR_INVALID;
  if (ul->chunk_x != c->x || ul->chunk_z != c->z) return LC_ERR_INVALID;

  lc_i64_arr_free(&c->sky_light_mask);
  lc_i64_arr_free(&c->block_light_mask);
  lc_i64_arr_free(&c->empty_sky_light_mask);
  lc_i64_arr_free(&c->empty_block_light_mask);
  lc_u8_grid_free(&c->sky_light);
  lc_u8_grid_free(&c->block_light);

  lc_status st = lc_copy_i64_arr(&ul->sky_light_mask, &c->sky_light_mask);
  if (st != LC_OK) return st;
  st = lc_copy_i64_arr(&ul->block_light_mask, &c->block_light_mask);
  if (st != LC_OK) return st;
  st = lc_copy_i64_arr(&ul->empty_sky_light_mask, &c->empty_sky_light_mask);
  if (st != LC_OK) return st;
  st = lc_copy_i64_arr(&ul->empty_block_light_mask, &c->empty_block_light_mask);
  if (st != LC_OK) return st;
  st = lc_copy_u8_grid(&ul->sky_light, &c->sky_light);
  if (st != LC_OK) return st;
  return lc_copy_u8_grid(&ul->block_light, &c->block_light);
}

lc_status lc_chunk_apply_block_change(lc_chunk *c, const lc_block_change *bc) {
  if (!c || !bc) return LC_ERR_INVALID;
  int32_t chunk_x = bc->location.x >> 4;
  int32_t chunk_z = bc->location.z >> 4;
  if (chunk_x != c->x || chunk_z != c->z) return LC_ERR_INVALID;

  int32_t sec_y = lc_section_y_for_block(bc->location.y, c->min_y);
  lc_chunk_section *sec = NULL;
  lc_status st = lc_chunk_get_or_create_section(c, sec_y, &sec);
  if (st != LC_OK) return st;

  int lx = bc->location.x & 15;
  int ly = bc->location.y - (sec_y << 4);
  int lz = bc->location.z & 15;
  if (ly < 0 || ly > 15) return LC_ERR_INVALID;
  sec->state_ids[lc_block_index(lx, ly, lz)] = bc->type;
  sec->solid_block_count = lc_count_solid_blocks(sec->state_ids);
  return LC_OK;
}

lc_status lc_chunk_apply_multi_block_change(lc_chunk *c, const lc_multi_block_change *mbc) {
  if (!c || !mbc) return LC_ERR_INVALID;
  if (mbc->chunk_coordinates.x != c->x || mbc->chunk_coordinates.z != c->z) return LC_ERR_INVALID;

  int32_t sec_y = mbc->chunk_coordinates.y;
  lc_chunk_section *sec = NULL;
  lc_status st = lc_chunk_get_or_create_section(c, sec_y, &sec);
  if (st != LC_OK) return st;

  for (size_t i = 0; i < mbc->record_count; i++) {
    int32_t record = mbc->records[i];
    int block_z = (record >> 4) & 0x0f;
    int block_x = (record >> 8) & 0x0f;
    int block_y = record & 0x0f;
    int32_t state_id = (uint32_t)record >> 12;
    sec->state_ids[lc_block_index(block_x, block_y, block_z)] = state_id;
  }
  sec->solid_block_count = lc_count_solid_blocks(sec->state_ids);
  return LC_OK;
}

lc_status lc_chunk_to_map_chunk(const lc_chunk *c, lc_map_chunk *out) {
  if (!c || !out) return LC_ERR_INVALID;
  lc_map_chunk tmp;
  memset(&tmp, 0, sizeof(tmp));
  tmp.x = c->x;
  tmp.z = c->z;

  lc_status st = lc_encode_chunk_data(c, &tmp.chunk_data);
  if (st != LC_OK) goto fail;
  st = lc_copy_heightmap_arr(&c->heightmaps, &tmp.heightmaps);
  if (st != LC_OK) goto fail;
  st = lc_copy_block_entity_arr(&c->block_entities, &tmp.block_entities);
  if (st != LC_OK) goto fail;
  st = lc_copy_i64_arr(&c->sky_light_mask, &tmp.sky_light_mask);
  if (st != LC_OK) goto fail;
  st = lc_copy_i64_arr(&c->block_light_mask, &tmp.block_light_mask);
  if (st != LC_OK) goto fail;
  st = lc_copy_i64_arr(&c->empty_sky_light_mask, &tmp.empty_sky_light_mask);
  if (st != LC_OK) goto fail;
  st = lc_copy_i64_arr(&c->empty_block_light_mask, &tmp.empty_block_light_mask);
  if (st != LC_OK) goto fail;
  st = lc_copy_u8_grid(&c->sky_light, &tmp.sky_light);
  if (st != LC_OK) goto fail;
  st = lc_copy_u8_grid(&c->block_light, &tmp.block_light);
  if (st != LC_OK) goto fail;

  *out = tmp;
  return LC_OK;
fail:
  lc_map_chunk_free(&tmp);
  return st;
}

lc_status lc_chunk_serialize(const lc_chunk *c, lc_byte_buf *out) {
  if (!c || !out) return LC_ERR_INVALID;
  memset(out, 0, sizeof(*out));
  lc_out_buf o;
  memset(&o, 0, sizeof(o));

  if (lc_out_i32_le(&o, (int32_t)LC_CHUNK_MAGIC) != LC_OK) goto oom;
  if (lc_out_i32_le(&o, LC_CHUNK_FMT_VERSION) != LC_OK) goto oom;
  if (lc_out_i32_le(&o, c->x) != LC_OK) goto oom;
  if (lc_out_i32_le(&o, c->z) != LC_OK) goto oom;
  if (lc_out_i32_le(&o, c->min_y) != LC_OK) goto oom;
  if (lc_out_i32_le(&o, c->world_height) != LC_OK) goto oom;

  if (lc_out_varint(&o, (int32_t)c->section_count) != LC_OK) goto oom;
  for (size_t i = 0; i < c->section_count; i++) {
    const lc_chunk_section *sec = &c->sections[i];
    if (lc_out_i32_le(&o, sec->section_y) != LC_OK) goto oom;
    if (lc_out_i16_le(&o, sec->solid_block_count) != LC_OK) goto oom;
    for (int j = 0; j < LC_BLOCK_VOLUME; j++) {
      if (lc_out_i32_le(&o, sec->state_ids[j]) != LC_OK) goto oom;
    }
  }

  if (lc_write_heightmaps(&o, &c->heightmaps) != LC_OK) goto oom;
  if (lc_write_block_entities(&o, &c->block_entities) != LC_OK) goto oom;
  if (lc_write_i64_array(&o, &c->sky_light_mask) != LC_OK) goto oom;
  if (lc_write_i64_array(&o, &c->block_light_mask) != LC_OK) goto oom;
  if (lc_write_i64_array(&o, &c->empty_sky_light_mask) != LC_OK) goto oom;
  if (lc_write_i64_array(&o, &c->empty_block_light_mask) != LC_OK) goto oom;
  if (lc_write_u8_grid(&o, &c->sky_light) != LC_OK) goto oom;
  if (lc_write_u8_grid(&o, &c->block_light) != LC_OK) goto oom;

  out->data = o.data;
  out->len = o.len;
  return LC_OK;
oom:
  lc_out_free(&o);
  return LC_ERR_OOM;
}

lc_status lc_chunk_deserialize(const uint8_t *data, size_t len, lc_chunk *out) {
  if (!data || !out) return LC_ERR_INVALID;
  memset(out, 0, sizeof(*out));
  lc_chunk_init(out);

  lc_buf b;
  lc_buf_init(&b, data, len);
  int32_t magic, version;
  if (lc_buf_read_i32_le(&b, &magic) != LC_OK) return LC_ERR_TRUNCATED;
  if (magic != (int32_t)LC_CHUNK_MAGIC) return LC_ERR_INVALID;
  if (lc_buf_read_i32_le(&b, &version) != LC_OK) return LC_ERR_TRUNCATED;
  if (version != LC_CHUNK_FMT_VERSION) return LC_ERR_INVALID;
  if (lc_buf_read_i32_le(&b, &out->x) != LC_OK) goto fail;
  if (lc_buf_read_i32_le(&b, &out->z) != LC_OK) goto fail;
  if (lc_buf_read_i32_le(&b, &out->min_y) != LC_OK) goto fail;
  if (lc_buf_read_i32_le(&b, &out->world_height) != LC_OK) goto fail;

  int32_t section_count;
  if (lc_buf_read_varint(&b, &section_count) != LC_OK) goto fail;
  if (section_count < 0) goto invalid;
  out->section_count = (size_t)section_count;
  if (section_count) {
    out->sections = (lc_chunk_section *)calloc((size_t)section_count, sizeof(lc_chunk_section));
    if (!out->sections) goto oom;
    for (int32_t i = 0; i < section_count; i++) {
      if (lc_buf_read_i32_le(&b, &out->sections[i].section_y) != LC_OK) goto fail;
      if (lc_buf_read_i16_le(&b, &out->sections[i].solid_block_count) != LC_OK) goto fail;
      for (int j = 0; j < LC_BLOCK_VOLUME; j++) {
        if (lc_buf_read_i32_le(&b, &out->sections[i].state_ids[j]) != LC_OK) goto fail;
      }
    }
  }

  if (lc_read_heightmaps(&b, &out->heightmaps) != LC_OK) goto fail;
  if (lc_read_block_entities(&b, &out->block_entities) != LC_OK) goto fail;
  if (lc_buf_read_i64_array(&b, &out->sky_light_mask) != LC_OK) goto fail;
  if (lc_buf_read_i64_array(&b, &out->block_light_mask) != LC_OK) goto fail;
  if (lc_buf_read_i64_array(&b, &out->empty_sky_light_mask) != LC_OK) goto fail;
  if (lc_buf_read_i64_array(&b, &out->empty_block_light_mask) != LC_OK) goto fail;
  if (lc_buf_read_u8_grid(&b, &out->sky_light) != LC_OK) goto fail;
  if (lc_buf_read_u8_grid(&b, &out->block_light) != LC_OK) goto fail;
  return LC_OK;
invalid:
  lc_chunk_free(out);
  return LC_ERR_INVALID;
oom:
  lc_chunk_free(out);
  return LC_ERR_OOM;
fail:
  lc_chunk_free(out);
  return LC_ERR_TRUNCATED;
}

int lc_chunk_to_string(const lc_chunk *c, char *buf, size_t buflen) {
  if (!c || !buf || buflen == 0) return 0;
  return lc_snprintf(buf, buflen,
                     "chunk{x=%d,z=%d,sections=%zu,heightmaps=%zu,blockEntities=%zu,"
                     "skyMask=%zu,blockMask=%zu,skyLightSections=%zu,blockLightSections=%zu}",
                     c->x, c->z, c->section_count, c->heightmaps.count, c->block_entities.count,
                     c->sky_light_mask.count, c->block_light_mask.count, c->sky_light.row_count,
                     c->block_light.row_count);
}

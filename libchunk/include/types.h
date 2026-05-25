#ifndef LIBCHUNK_TYPES_H
#define LIBCHUNK_TYPES_H

#include <stddef.h>
#include <stdint.h>

typedef enum lc_status {
  LC_OK = 0,
  LC_ERR_TRUNCATED = -1,
  LC_ERR_INVALID = -2,
  LC_ERR_OOM = -3,
} lc_status;

typedef struct lc_vec3 {
  double x, y, z;
} lc_vec3;

typedef struct lc_block_pos {
  int32_t x, y, z;
} lc_block_pos;

typedef struct lc_chunk_section_pos {
  int32_t x, z, y; /* section chunk coords from multi_block_change */
} lc_chunk_section_pos;

typedef struct lc_uuid {
  uint8_t bytes[16];
} lc_uuid;

typedef struct lc_byte_buf {
  uint8_t *data;
  size_t len;
} lc_byte_buf;

typedef struct lc_i64_arr {
  int64_t *values;
  size_t count;
} lc_i64_arr;

typedef struct lc_u8_arr {
  uint8_t *values;
  size_t count;
} lc_u8_arr;

typedef struct lc_u8_grid {
  uint8_t **rows;
  size_t row_count;
  size_t *row_lens;
} lc_u8_grid;

typedef struct lc_heightmap {
  const char *type_name;
  int type_id;
  lc_i64_arr data;
} lc_heightmap;

typedef struct lc_heightmap_arr {
  lc_heightmap *items;
  size_t count;
} lc_heightmap_arr;

typedef struct lc_block_entity {
  uint8_t x, z; /* 4-bit chunk-local */
  int16_t y;
  int32_t type;
  lc_byte_buf nbt; /* raw anonymous NBT bytes, may be empty */
} lc_block_entity;

typedef struct lc_block_entity_arr {
  lc_block_entity *items;
  size_t count;
} lc_block_entity_arr;

typedef enum lc_meta_value_kind {
  LC_META_NONE = 0,
  LC_META_BYTE,
  LC_META_INT,
  LC_META_LONG,
  LC_META_FLOAT,
  LC_META_DOUBLE,
  LC_META_STRING,
  LC_META_BOOL,
  LC_META_VARINT,
  LC_META_RAW, /* complex / unparsed — raw value bytes */
} lc_meta_value_kind;

typedef struct lc_metadata_entry {
  uint8_t key;
  int type_id;
  const char *type_name;
  lc_meta_value_kind kind;
  union {
    int8_t i8;
    int32_t i32;
    int64_t i64;
    float f32;
    double f64;
    uint8_t boolean;
    char *string;
    lc_byte_buf raw;
  } v;
} lc_metadata_entry;

typedef struct lc_metadata_arr {
  lc_metadata_entry *items;
  size_t count;
} lc_metadata_arr;

typedef struct lc_equipment {
  int8_t slot;
  int32_t item_count;
  int32_t item_id; /* -1 if empty stack */
  lc_byte_buf item_extra; /* remainder of Slot if non-empty */
} lc_equipment;

typedef struct lc_equipment_arr {
  lc_equipment *items;
  size_t count;
} lc_equipment_arr;

typedef struct lc_registry_entry {
  char *key;
  lc_byte_buf nbt; /* absent => len 0 */
} lc_registry_entry;

typedef struct lc_registry_entry_arr {
  lc_registry_entry *items;
  size_t count;
} lc_registry_entry_arr;

#endif /* LIBCHUNK_TYPES_H */

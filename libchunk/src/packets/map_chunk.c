#include "../internal.h"

static const char *HEIGHTMAP_NAMES[] = {
    "world_surface_wg", "world_surface", "ocean_floor_wg", "ocean_floor",
    "motion_blocking", "motion_blocking_no_leaves",
};

const char *lc_heightmap_type_name(int id) {
  if (id >= 0 && id < (int)(sizeof(HEIGHTMAP_NAMES) / sizeof(HEIGHTMAP_NAMES[0])))
    return HEIGHTMAP_NAMES[id];
  return "unknown";
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

static lc_status lc_read_block_entities(lc_buf *b, lc_block_entity_arr *out) {
  int32_t count;
  if (lc_buf_read_varint(b, &count) != LC_OK) return LC_ERR_TRUNCATED;
  if (count < 0) return LC_ERR_INVALID;
  out->count = (size_t)count;
  out->items = count ? (lc_block_entity *)calloc((size_t)count, sizeof(lc_block_entity)) : NULL;
  if (count && !out->items) return LC_ERR_OOM;
  for (int32_t i = 0; i < count; i++) {
    static const lc_bitfield_spec xz[] = {{4, 0}, {4, 0}};
    int32_t xz_vals[2];
    if (lc_buf_read_bitfield(b, xz, 2, xz_vals) != LC_OK) goto fail;
    out->items[i].x = (uint8_t)xz_vals[0];
    out->items[i].z = (uint8_t)xz_vals[1];
    if (lc_buf_read_i16_be(b, &out->items[i].y) != LC_OK) goto fail;
    if (lc_buf_read_varint(b, &out->items[i].type) != LC_OK) goto fail;
    out->items[i].nbt.data = NULL;
    out->items[i].nbt.len = 0;
    if (lc_nbt_skip_anon_optional(b) != LC_OK) goto fail;
  }
  return LC_OK;
fail:
  lc_block_entity_arr_free(out);
  return LC_ERR_TRUNCATED;
}

void lc_heightmap_arr_free(lc_heightmap_arr *a) {
  if (!a->items) return;
  for (size_t i = 0; i < a->count; i++) lc_i64_arr_free(&a->items[i].data);
  free(a->items);
  a->items = NULL;
  a->count = 0;
}

void lc_block_entity_arr_free(lc_block_entity_arr *a) {
  if (!a->items) return;
  for (size_t i = 0; i < a->count; i++) lc_byte_buf_free(&a->items[i].nbt);
  free(a->items);
  a->items = NULL;
  a->count = 0;
}

lc_status lc_parse_map_chunk(const uint8_t *data, size_t len, lc_map_chunk *out) {
  memset(out, 0, sizeof(*out));
  lc_buf b;
  lc_buf_init(&b, data, len);
  if (lc_buf_read_i32_be(&b, &out->x) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_i32_be(&b, &out->z) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_read_heightmaps(&b, &out->heightmaps) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_byte_array(&b, &out->chunk_data) != LC_OK) goto fail;
  if (lc_read_block_entities(&b, &out->block_entities) != LC_OK) goto fail;
  if (lc_buf_read_i64_array(&b, &out->sky_light_mask) != LC_OK) goto fail;
  if (lc_buf_read_i64_array(&b, &out->block_light_mask) != LC_OK) goto fail;
  if (lc_buf_read_i64_array(&b, &out->empty_sky_light_mask) != LC_OK) goto fail;
  if (lc_buf_read_i64_array(&b, &out->empty_block_light_mask) != LC_OK) goto fail;
  if (lc_buf_read_u8_grid(&b, &out->sky_light) != LC_OK) goto fail;
  if (lc_buf_read_u8_grid(&b, &out->block_light) != LC_OK) goto fail;
  return LC_OK;
fail:
  lc_map_chunk_free(out);
  return LC_ERR_TRUNCATED;
}

void lc_map_chunk_free(lc_map_chunk *p) {
  if (!p) return;
  lc_heightmap_arr_free(&p->heightmaps);
  lc_byte_buf_free(&p->chunk_data);
  lc_block_entity_arr_free(&p->block_entities);
  lc_i64_arr_free(&p->sky_light_mask);
  lc_i64_arr_free(&p->block_light_mask);
  lc_i64_arr_free(&p->empty_sky_light_mask);
  lc_i64_arr_free(&p->empty_block_light_mask);
  lc_u8_grid_free(&p->sky_light);
  lc_u8_grid_free(&p->block_light);
  memset(p, 0, sizeof(*p));
}

int lc_map_chunk_to_string(const lc_map_chunk *p, char *buf, size_t buflen) {
  if (!p || !buf || buflen == 0) return 0;
  int w = lc_snprintf(buf, buflen,
                      "map_chunk{x=%d,z=%d,heightmaps=%zu,chunkData=%zu bytes,blockEntities=%zu,"
                      "skyMask=%zu,blockMask=%zu,skyLightSections=%zu,blockLightSections=%zu}",
                      p->x, p->z, p->heightmaps.count, p->chunk_data.len, p->block_entities.count,
                      p->sky_light_mask.count, p->block_light_mask.count, p->sky_light.row_count,
                      p->block_light.row_count);
  return w;
}

#include "internal.h"

static const char *HEIGHTMAP_NAMES[] = {
    "world_surface_wg", "world_surface", "ocean_floor_wg", "ocean_floor",
    "motion_blocking", "motion_blocking_no_leaves",
};

const char *lc_heightmap_type_name(int id) {
  if (id >= 0 && id < (int)(sizeof(HEIGHTMAP_NAMES) / sizeof(HEIGHTMAP_NAMES[0])))
    return HEIGHTMAP_NAMES[id];
  return "unknown";
}
/* Good for: Decode Minecraft wire payload for spawn info into a struct.
 * Callers: packets.c (same file), play_stream.c, respawn.c.
 */

lc_status lc_parse_spawn_info(lc_buf *b, lc_spawn_info *out) {
  memset(out, 0, sizeof(*out));
  if (lc_buf_read_varint(b, &out->dimension) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_string(b, &out->name) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_i64_be(b, &out->hashed_seed) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_i8(b, &out->gamemode) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_u8(b, &out->previous_gamemode) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_bool(b, &out->is_debug) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_bool(b, &out->is_flat) != LC_OK) return LC_ERR_TRUNCATED;
  {
    uint8_t present;
    if (lc_buf_read_u8(b, &present) != LC_OK) return LC_ERR_TRUNCATED;
    out->has_death = present;
    if (present) {
      if (lc_buf_read_string(b, &out->death_dimension_name) != LC_OK) return LC_ERR_TRUNCATED;
      if (lc_buf_read_position(b, &out->death_pos) != LC_OK) return LC_ERR_TRUNCATED;
    }
  }
  if (lc_buf_read_varint(b, &out->portal_cooldown) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_varint(b, &out->sea_level) != LC_OK) return LC_ERR_TRUNCATED;
  return LC_OK;
}
/* Good for: Release heap owned by lc_spawn info.
 * Callers: packets.c (same file), packets_write.c, play_stream.c, respawn.c.
 */

void lc_spawn_info_free(lc_spawn_info *s) {
  free(s->name);
  free(s->death_dimension_name);
  s->name = NULL;
  s->death_dimension_name = NULL;
}
/* Good for: Decode heightmaps from map_chunk wire.
 * Callers: chunk.c, map_chunk.c, packets.c (same file).
 */

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
/* Good for: Decode block entity list from map_chunk wire.
 * Callers: chunk.c, map_chunk.c, packets.c (same file).
 */

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
    uint8_t nbt_present;
    if (lc_nbt_read_anon_optional(b, &out->items[i].nbt, &nbt_present) != LC_OK) goto fail;
  }
  return LC_OK;
fail:
  lc_block_entity_arr_free(out);
  return LC_ERR_TRUNCATED;
}
/* Good for: Release heap owned by lc_heightmap arr.
 * Callers: chunk.c, map_chunk.c, packets.c (same file).
 */

void lc_heightmap_arr_free(lc_heightmap_arr *a) {
  if (!a->items) return;
  for (size_t i = 0; i < a->count; i++) lc_i64_arr_free(&a->items[i].data);
  free(a->items);
  a->items = NULL;
  a->count = 0;
}
/* Good for: Release heap owned by lc_block entity arr.
 * Callers: chunk.c, map_chunk.c, packets.c (same file).
 */

void lc_block_entity_arr_free(lc_block_entity_arr *a) {
  if (!a->items) return;
  for (size_t i = 0; i < a->count; i++) lc_byte_buf_free(&a->items[i].nbt);
  free(a->items);
  a->items = NULL;
  a->count = 0;
}
/* Good for: Decode Minecraft wire payload for map chunk into a struct.
 * Callers: chunk_stream_receiver.c, decode_raw_dir.c, decode_wire.c, list_map_surface.c, mc_s2c_log.c, summarize_raw_dir.c.
 */

lc_status lc_peek_map_chunk_coords(const uint8_t *data, size_t len, int32_t *x, int32_t *z) {
  if (!data || !x || !z) return LC_ERR_INVALID;
  lc_buf b;
  lc_buf_init(&b, data, len);
  if (lc_buf_read_i32_be(&b, x) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_i32_be(&b, z) != LC_OK) return LC_ERR_TRUNCATED;
  return LC_OK;
}

lc_status lc_parse_map_chunk(const uint8_t *data, size_t len, lc_map_chunk *out) {
  memset(out, 0, sizeof(*out));
  lc_buf b;
  lc_buf_init(&b, data, len);
  /* 1.21.x map_chunk x/z are big-endian (minecraft-data / protodef wire). */
  if (lc_buf_read_i32_be(&b, &out->x) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_i32_be(&b, &out->z) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_read_heightmaps(&b, &out->heightmaps) != LC_OK) goto fail;
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
/* Good for: Release heap owned by lc_map chunk.
 * Callers: chunk.c, chunk_stream_receiver.c, decode_raw_dir.c, decode_wire.c, list_map_surface.c, map_chunk.c, mc_s2c_log.c, mc_wire_templates.c, packets.c (same file), summarize_raw_dir.c.
 */

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
/* Good for: Decode Minecraft wire payload for update light into a struct.
 * Callers: chunk_stream_receiver.c, decode_raw_dir.c, decode_wire.c, mc_s2c_log.c.
 */

lc_status lc_parse_update_light(const uint8_t *data, size_t len, lc_update_light *out) {
  memset(out, 0, sizeof(*out));
  lc_buf b;
  lc_buf_init(&b, data, len);
  if (lc_buf_read_varint(&b, &out->chunk_x) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_varint(&b, &out->chunk_z) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_i64_array(&b, &out->sky_light_mask) != LC_OK) goto fail;
  if (lc_buf_read_i64_array(&b, &out->block_light_mask) != LC_OK) goto fail;
  if (lc_buf_read_i64_array(&b, &out->empty_sky_light_mask) != LC_OK) goto fail;
  if (lc_buf_read_i64_array(&b, &out->empty_block_light_mask) != LC_OK) goto fail;
  if (lc_buf_read_u8_grid(&b, &out->sky_light) != LC_OK) goto fail;
  if (lc_buf_read_u8_grid(&b, &out->block_light) != LC_OK) goto fail;
  return LC_OK;
fail:
  lc_update_light_free(out);
  return LC_ERR_TRUNCATED;
}
/* Good for: Release heap owned by lc_update light.
 * Callers: chunk_stream_receiver.c, decode_raw_dir.c, decode_wire.c, mc_s2c_log.c, packets.c (same file), update_light.c.
 */

void lc_update_light_free(lc_update_light *p) {
  if (!p) return;
  lc_i64_arr_free(&p->sky_light_mask);
  lc_i64_arr_free(&p->block_light_mask);
  lc_i64_arr_free(&p->empty_sky_light_mask);
  lc_i64_arr_free(&p->empty_block_light_mask);
  lc_u8_grid_free(&p->sky_light);
  lc_u8_grid_free(&p->block_light);
  memset(p, 0, sizeof(*p));
}
/* Good for: Decode Minecraft wire payload for block change into a struct.
 * Callers: chunk_stream_receiver.c, decode_wire.c.
 */

lc_status lc_parse_block_change(const uint8_t *data, size_t len, lc_block_change *out) {
  lc_buf b;
  lc_buf_init(&b, data, len);
  if (lc_buf_read_position(&b, &out->location) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_varint(&b, &out->type) != LC_OK) return LC_ERR_TRUNCATED;
  return LC_OK;
}
/* Good for: Decode Minecraft wire payload for unload chunk into a struct.
 * Callers: chunk_stream_receiver.c, decode_wire.c.
 */

lc_status lc_parse_unload_chunk(const uint8_t *data, size_t len, lc_unload_chunk *out) {
  lc_buf b;
  lc_buf_init(&b, data, len);
  /* Wire order: chunkZ, chunkX (minecraft-data packet_unload_chunk). */
  if (lc_buf_read_i32_be(&b, &out->z) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_i32_be(&b, &out->x) != LC_OK) return LC_ERR_TRUNCATED;
  return LC_OK;
}
/* Good for: Decode Minecraft wire payload for tile entity data into a struct.
 * Callers: chunk_stream_receiver.c, decode_raw_dir.c, decode_wire.c.
 */

lc_status lc_parse_tile_entity_data(const uint8_t *data, size_t len, lc_tile_entity_data *out) {
  memset(out, 0, sizeof(*out));
  lc_buf b;
  lc_buf_init(&b, data, len);
  if (lc_buf_read_position(&b, &out->location) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_varint(&b, &out->action) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_nbt_read_anon_optional(&b, &out->nbt, &out->nbt_present) != LC_OK) return LC_ERR_TRUNCATED;
  return LC_OK;
}
/* Good for: Release heap owned by lc_tile entity data.
 * Callers: chunk_stream_receiver.c, decode_raw_dir.c, decode_wire.c.
 */

void lc_tile_entity_data_free(lc_tile_entity_data *p) {
  if (!p) return;
  lc_byte_buf_free(&p->nbt);
  memset(p, 0, sizeof(*p));
}
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
 * Callers: chunk_stream_receiver.c, decode_wire.c, multi_block_change.c, packets.c (same file).
 */

void lc_multi_block_change_free(lc_multi_block_change *p) {
  free(p->records);
  p->records = NULL;
  p->record_count = 0;
}
/* Good for: Decode Minecraft wire payload for spawn entity into a struct.
 * Callers: chunk_stream_receiver.c, decode_wire.c.
 */

lc_status lc_parse_spawn_entity(const uint8_t *data, size_t len, lc_spawn_entity *out) {
  lc_buf b;
  lc_buf_init(&b, data, len);
  if (lc_buf_read_varint(&b, &out->entity_id) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_uuid(&b, &out->object_uuid) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_varint(&b, &out->type) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_f64_le(&b, &out->x) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_f64_le(&b, &out->y) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_f64_le(&b, &out->z) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_lpvec3(&b, &out->velocity) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_i8(&b, &out->pitch) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_i8(&b, &out->yaw) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_i8(&b, &out->head_pitch) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_varint(&b, &out->object_data) != LC_OK) return LC_ERR_TRUNCATED;
  return LC_OK;
}
/* Good for: Decode Minecraft wire payload for entity metadata into a struct.
 * Callers: chunk_stream_receiver.c, decode_wire.c.
 */

lc_status lc_parse_entity_metadata(const uint8_t *data, size_t len, lc_entity_metadata *out) {
  memset(out, 0, sizeof(*out));
  lc_buf b;
  lc_buf_init(&b, data, len);
  if (lc_buf_read_varint(&b, &out->entity_id) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_metadata_read_loop(&b, &out->metadata) != LC_OK) return LC_ERR_TRUNCATED;
  return LC_OK;
}
/* Good for: Release heap owned by lc_entity metadata.
 * Callers: chunk_stream_receiver.c, decode_wire.c.
 */

void lc_entity_metadata_free(lc_entity_metadata *p) {
  lc_metadata_arr_free(&p->metadata);
  memset(p, 0, sizeof(*p));
}
/* Good for: Decode Minecraft wire payload for entity equipment into a struct.
 * Callers: chunk_stream_receiver.c, decode_raw_dir.c, decode_wire.c.
 */

lc_status lc_parse_entity_equipment(const uint8_t *data, size_t len, lc_entity_equipment *out) {
  memset(out, 0, sizeof(*out));
  lc_buf b;
  lc_buf_init(&b, data, len);
  if (lc_buf_read_varint(&b, &out->entity_id) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_read_top_bit_array(&b, &out->equipments) != LC_OK) return LC_ERR_TRUNCATED;
  return LC_OK;
}
/* Good for: Release heap owned by lc_entity equipment.
 * Callers: chunk_stream_receiver.c, decode_raw_dir.c, decode_wire.c.
 */

void lc_entity_equipment_free(lc_entity_equipment *p) {
  lc_equipment_arr_free(&p->equipments);
  memset(p, 0, sizeof(*p));
}
/* Good for: Decode Minecraft wire payload for entity destroy into a struct.
 * Callers: chunk_stream_receiver.c, decode_wire.c.
 */

lc_status lc_parse_entity_destroy(const uint8_t *data, size_t len, lc_entity_destroy *out) {
  memset(out, 0, sizeof(*out));
  lc_buf b;
  lc_buf_init(&b, data, len);
  int32_t count;
  if (lc_buf_read_varint(&b, &count) != LC_OK) return LC_ERR_TRUNCATED;
  if (count < 0) return LC_ERR_INVALID;
  out->count = (size_t)count;
  out->entity_ids = count ? (int32_t *)malloc((size_t)count * sizeof(int32_t)) : NULL;
  if (count && !out->entity_ids) return LC_ERR_OOM;
  for (int32_t i = 0; i < count; i++) {
    if (lc_buf_read_varint(&b, &out->entity_ids[i]) != LC_OK) {
      lc_entity_destroy_free(out);
      return LC_ERR_TRUNCATED;
    }
  }
  return LC_OK;
}
/* Good for: Release heap owned by lc_entity destroy.
 * Callers: chunk_stream_receiver.c, decode_wire.c, entity_destroy.c, packets.c (same file).
 */

void lc_entity_destroy_free(lc_entity_destroy *p) {
  free(p->entity_ids);
  p->entity_ids = NULL;
  p->count = 0;
}
/* Good for: Decode Minecraft wire payload for set passengers into a struct.
 * Callers: chunk_stream_receiver.c, decode_wire.c.
 */

lc_status lc_parse_set_passengers(const uint8_t *data, size_t len, lc_set_passengers *out) {
  memset(out, 0, sizeof(*out));
  lc_buf b;
  lc_buf_init(&b, data, len);
  if (lc_buf_read_varint(&b, &out->entity_id) != LC_OK) return LC_ERR_TRUNCATED;
  int32_t count;
  if (lc_buf_read_varint(&b, &count) != LC_OK) return LC_ERR_TRUNCATED;
  if (count < 0) return LC_ERR_INVALID;
  out->passenger_count = (size_t)count;
  out->passengers = count ? (int32_t *)malloc((size_t)count * sizeof(int32_t)) : NULL;
  if (count && !out->passengers) return LC_ERR_OOM;
  for (int32_t i = 0; i < count; i++) {
    if (lc_buf_read_varint(&b, &out->passengers[i]) != LC_OK) {
      lc_set_passengers_free(out);
      return LC_ERR_TRUNCATED;
    }
  }
  return LC_OK;
}
/* Good for: Release heap owned by lc_set passengers.
 * Callers: chunk_stream_receiver.c, decode_wire.c, packets.c (same file), set_passengers.c.
 */

void lc_set_passengers_free(lc_set_passengers *p) {
  free(p->passengers);
  p->passengers = NULL;
  p->passenger_count = 0;
}
/* Good for: Decode Minecraft wire payload for rel entity move into a struct.
 * Callers: chunk_stream_receiver.c, decode_wire.c.
 */

lc_status lc_parse_rel_entity_move(const uint8_t *data, size_t len, lc_rel_entity_move *out) {
  lc_buf b;
  lc_buf_init(&b, data, len);
  if (lc_buf_read_varint(&b, &out->entity_id) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_i16_le(&b, &out->dx) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_i16_le(&b, &out->dy) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_i16_le(&b, &out->dz) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_bool(&b, &out->on_ground) != LC_OK) return LC_ERR_TRUNCATED;
  return LC_OK;
}
/* Good for: Decode Minecraft wire payload for entity move look into a struct.
 * Callers: chunk_stream_receiver.c, decode_wire.c.
 */

lc_status lc_parse_entity_move_look(const uint8_t *data, size_t len, lc_entity_move_look *out) {
  lc_buf b;
  lc_buf_init(&b, data, len);
  if (lc_buf_read_varint(&b, &out->entity_id) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_i16_le(&b, &out->dx) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_i16_le(&b, &out->dy) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_i16_le(&b, &out->dz) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_i8(&b, &out->yaw) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_i8(&b, &out->pitch) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_bool(&b, &out->on_ground) != LC_OK) return LC_ERR_TRUNCATED;
  return LC_OK;
}
/* Good for: Decode Minecraft wire payload for sync entity position into a struct.
 * Callers: chunk_stream_receiver.c, decode_wire.c.
 */

lc_status lc_parse_sync_entity_position(const uint8_t *data, size_t len, lc_sync_entity_position *out) {
  lc_buf b;
  lc_buf_init(&b, data, len);
  if (lc_buf_read_varint(&b, &out->entity_id) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_f64_le(&b, &out->x) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_f64_le(&b, &out->y) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_f64_le(&b, &out->z) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_f64_le(&b, &out->dx) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_f64_le(&b, &out->dy) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_f64_le(&b, &out->dz) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_f32_le(&b, &out->yaw) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_f32_le(&b, &out->pitch) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_bool(&b, &out->on_ground) != LC_OK) return LC_ERR_TRUNCATED;
  return LC_OK;
}
/* Good for: Decode Minecraft wire payload for entity velocity into a struct.
 * Callers: chunk_stream_receiver.c, decode_wire.c.
 */

lc_status lc_parse_entity_velocity(const uint8_t *data, size_t len, lc_entity_velocity *out) {
  lc_buf b;
  lc_buf_init(&b, data, len);
  if (lc_buf_read_varint(&b, &out->entity_id) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_lpvec3(&b, &out->velocity) != LC_OK) return LC_ERR_TRUNCATED;
  return LC_OK;
}
/* Good for: Decode Minecraft wire payload for entity head rotation into a struct.
 * Callers: chunk_stream_receiver.c, decode_wire.c.
 */

lc_status lc_parse_entity_head_rotation(const uint8_t *data, size_t len, lc_entity_head_rotation *out) {
  lc_buf b;
  lc_buf_init(&b, data, len);
  if (lc_buf_read_varint(&b, &out->entity_id) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_i8(&b, &out->head_yaw) != LC_OK) return LC_ERR_TRUNCATED;
  return LC_OK;
}

const char *lc_entity_attribute_key_name(int32_t key) {
  static const char *keys[] = {
      "generic.armor",
      "generic.armor_toughness",
      "generic.attack_damage",
      "generic.attack_knockback",
      "generic.attack_speed",
      "player.block_break_speed",
      "player.block_interaction_range",
      "burning_time",
      "camera_distance",
      "explosion_knockback_resistance",
      "player.entity_interaction_range",
      "generic.fall_damage_multiplier",
      "generic.flying_speed",
      "generic.follow_range",
      "generic.gravity",
      "generic.jump_strength",
      "generic.knockback_resistance",
      "generic.luck",
      "generic.max_absorption",
      "generic.max_health",
      "generic.movement_speed",
      "generic.safe_fall_distance",
      "generic.scale",
      "zombie.spawn_reinforcements",
      "generic.step_height",
      "submerged_mining_speed",
      "sweeping_damage_ratio",
      "tempt_range",
      "water_movement_efficiency",
      "waypoint_transmit_range",
      "waypoint_receive_range",
  };
  if (key >= 0 && (size_t)key < sizeof(keys) / sizeof(keys[0])) return keys[key];
  return "unknown";
}
/* Good for: Release heap owned by lc_entity attribute modifier.
 * Callers: packets.c (same file).
 */

static void lc_entity_attribute_modifier_free(lc_entity_attribute_modifier *m) {
  if (!m) return;
  free(m->uuid);
  m->uuid = NULL;
}
/* Good for: Release heap owned by lc_entity attribute property.
 * Callers: packets.c (same file).
 */

static void lc_entity_attribute_property_free(lc_entity_attribute_property *p) {
  if (!p) return;
  for (size_t i = 0; i < p->modifier_count; i++) lc_entity_attribute_modifier_free(&p->modifiers[i]);
  free(p->modifiers);
  p->modifiers = NULL;
  p->modifier_count = 0;
}
/* Good for: Release heap owned by lc_entity update attributes.
 * Callers: chunk_stream_receiver.c, decode_raw_dir.c, decode_wire.c, packets.c (same file).
 */

void lc_entity_update_attributes_free(lc_entity_update_attributes *p) {
  if (!p) return;
  for (size_t i = 0; i < p->property_count; i++) lc_entity_attribute_property_free(&p->properties[i]);
  free(p->properties);
  p->properties = NULL;
  p->property_count = 0;
  memset(p, 0, sizeof(*p));
}

lc_status lc_parse_entity_update_attributes(const uint8_t *data, size_t len,
                                            lc_entity_update_attributes *out) {
  memset(out, 0, sizeof(*out));
  lc_buf b;
  lc_buf_init(&b, data, len);
  if (lc_buf_read_varint(&b, &out->entity_id) != LC_OK) return LC_ERR_TRUNCATED;

  int32_t prop_count;
  if (lc_buf_read_varint(&b, &prop_count) != LC_OK) return LC_ERR_TRUNCATED;
  if (prop_count < 0) return LC_ERR_INVALID;
  out->property_count = (size_t)prop_count;
  out->properties =
      prop_count ? (lc_entity_attribute_property *)calloc((size_t)prop_count, sizeof(*out->properties))
                 : NULL;
  if (prop_count && !out->properties) return LC_ERR_OOM;

  for (int32_t pi = 0; pi < prop_count; pi++) {
    lc_entity_attribute_property *prop = &out->properties[pi];
    if (lc_buf_read_varint(&b, &prop->key) != LC_OK) goto fail;
    if (lc_buf_read_f64_le(&b, &prop->value) != LC_OK) goto fail;

    int32_t mod_count;
    if (lc_buf_read_varint(&b, &mod_count) != LC_OK) goto fail;
    if (mod_count < 0) goto fail;
    prop->modifier_count = (size_t)mod_count;
    prop->modifiers = mod_count ? (lc_entity_attribute_modifier *)calloc((size_t)mod_count,
                                                                         sizeof(*prop->modifiers))
                                : NULL;
    if (mod_count && !prop->modifiers) goto fail;

    for (int32_t mi = 0; mi < mod_count; mi++) {
      lc_entity_attribute_modifier *mod = &prop->modifiers[mi];
      if (lc_buf_read_string(&b, &mod->uuid) != LC_OK) goto fail;
      if (lc_buf_read_f64_le(&b, &mod->amount) != LC_OK) goto fail;
      if (lc_buf_read_i8(&b, &mod->operation) != LC_OK) goto fail;
    }
  }
  return LC_OK;

fail:
  lc_entity_update_attributes_free(out);
  return LC_ERR_TRUNCATED;
}
/* Good for: Decode Minecraft wire payload for position into a struct.
 * Callers: chunk_stream_receiver.c, decode_wire.c.
 */

lc_status lc_parse_position(const uint8_t *data, size_t len, lc_position *out) {
  lc_buf b;
  lc_buf_init(&b, data, len);
  if (lc_buf_read_varint(&b, &out->teleport_id) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_f64_le(&b, &out->x) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_f64_le(&b, &out->y) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_f64_le(&b, &out->z) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_f64_le(&b, &out->dx) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_f64_le(&b, &out->dy) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_f64_le(&b, &out->dz) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_f32_le(&b, &out->yaw) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_f32_le(&b, &out->pitch) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_u32_be(&b, &out->flags) != LC_OK) return LC_ERR_TRUNCATED;
  return LC_OK;
}
/* Good for: Decode Minecraft wire payload for respawn into a struct.
 * Callers: decode_wire.c.
 */

lc_status lc_parse_respawn(const uint8_t *data, size_t len, lc_respawn *out) {
  memset(out, 0, sizeof(*out));
  lc_buf b;
  lc_buf_init(&b, data, len);
  if (lc_parse_spawn_info(&b, &out->world_state) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_u8(&b, &out->copy_metadata) != LC_OK) {
    lc_spawn_info_free(&out->world_state);
    return LC_ERR_TRUNCATED;
  }
  return LC_OK;
}
/* Good for: Release heap owned by lc_respawn.
 * Callers: decode_wire.c.
 */

void lc_respawn_free(lc_respawn *p) {
  lc_spawn_info_free(&p->world_state);
  memset(p, 0, sizeof(*p));
}
/* Good for: Decode Minecraft wire payload for initialize world border into a struct.
 * Callers: decode_wire.c, play_stream.c.
 */

lc_status lc_parse_initialize_world_border(const uint8_t *data, size_t len, lc_initialize_world_border *out) {
  lc_buf b;
  lc_buf_init(&b, data, len);
  if (lc_buf_read_f64_le(&b, &out->x) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_f64_le(&b, &out->z) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_f64_le(&b, &out->old_diameter) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_f64_le(&b, &out->new_diameter) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_varint(&b, &out->speed) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_varint(&b, &out->portal_teleport_boundary) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_varint(&b, &out->warning_blocks) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_varint(&b, &out->warning_time) != LC_OK) return LC_ERR_TRUNCATED;
  return LC_OK;
}
/* Good for: Decode Minecraft wire payload for registry data into a struct.
 * Callers: decode_raw_dir.c, decode_wire.c, mc_static_registries.c.
 */

lc_status lc_parse_registry_data(const uint8_t *data, size_t len, lc_registry_data *out) {
  memset(out, 0, sizeof(*out));
  lc_buf b;
  lc_buf_init(&b, data, len);
  if (lc_buf_read_string(&b, &out->id) != LC_OK) return LC_ERR_TRUNCATED;
  int32_t count;
  if (lc_buf_read_varint(&b, &count) != LC_OK) goto fail;
  if (count < 0) {
    free(out->id);
    out->id = NULL;
    return LC_ERR_INVALID;
  }
  out->entries.count = (size_t)count;
  out->entries.items = count ? (lc_registry_entry *)calloc((size_t)count, sizeof(lc_registry_entry)) : NULL;
  if (count && !out->entries.items) goto fail;
  for (int32_t i = 0; i < count; i++) {
    if (lc_buf_read_string(&b, &out->entries.items[i].key) != LC_OK) goto fail;
    uint8_t present;
    if (lc_nbt_read_anon_optional(&b, &out->entries.items[i].nbt, &present) != LC_OK) goto fail;
    if (!present) {
      out->entries.items[i].nbt.data = NULL;
      out->entries.items[i].nbt.len = 0;
    }
  }
  return LC_OK;
fail:
  lc_registry_data_free(out);
  return LC_ERR_TRUNCATED;
}
/* Good for: Release heap owned by lc_registry entry arr.
 * Callers: packets.c (same file), registry_data.c.
 */

void lc_registry_entry_arr_free(lc_registry_entry_arr *a) {
  if (!a->items) return;
  for (size_t i = 0; i < a->count; i++) {
    free(a->items[i].key);
    lc_byte_buf_free(&a->items[i].nbt);
  }
  free(a->items);
  a->items = NULL;
  a->count = 0;
}
/* Good for: Release heap owned by lc_registry data.
 * Callers: decode_raw_dir.c, decode_wire.c, mc_static_registries.c, packets.c (same file), registry_data.c.
 */

void lc_registry_data_free(lc_registry_data *p) {
  free(p->id);
  lc_registry_entry_arr_free(&p->entries);
  memset(p, 0, sizeof(*p));
}

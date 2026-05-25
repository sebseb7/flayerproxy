#ifndef LIBCHUNK_STRUCT_CHUNK_H
#define LIBCHUNK_STRUCT_CHUNK_H

#include "../types.h"

#define LC_BLOCK_VOLUME 4096
#define LC_CHUNK_DEFAULT_MIN_Y (-64)
#define LC_CHUNK_DEFAULT_WORLD_HEIGHT 384

#define LC_BIOME_VOLUME 64

/** One 16×16×16 section keyed by Anvil section Y (world_y >> 4). */
typedef struct lc_chunk_section {
  int32_t section_y;
  int16_t solid_block_count;
  int32_t state_ids[LC_BLOCK_VOLUME];
  uint8_t has_biomes;
  int32_t biome_ids[LC_BIOME_VOLUME];
} lc_chunk_section;

/**
 * Merged chunk column: map_chunk base + block/light deltas.
 * Block state ids are unpacked per section; chunk_data is rebuilt on export.
 */
typedef struct lc_chunk {
  int32_t x, z;
  int32_t min_y;
  int32_t world_height;
  lc_chunk_section *sections;
  size_t section_count;
  lc_heightmap_arr heightmaps;
  lc_block_entity_arr block_entities;
  lc_i64_arr sky_light_mask;
  lc_i64_arr block_light_mask;
  lc_i64_arr empty_sky_light_mask;
  lc_i64_arr empty_block_light_mask;
  lc_u8_grid sky_light;
  lc_u8_grid block_light;
} lc_chunk;

#endif /* LIBCHUNK_STRUCT_CHUNK_H */

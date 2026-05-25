#ifndef LIBCHUNK_STRUCT_MAP_CHUNK_H
#define LIBCHUNK_STRUCT_MAP_CHUNK_H

#include "../types.h"

typedef struct lc_map_chunk {
  int32_t x, z;
  lc_heightmap_arr heightmaps;
  lc_byte_buf chunk_data;
  lc_block_entity_arr block_entities;
  lc_i64_arr sky_light_mask;
  lc_i64_arr block_light_mask;
  lc_i64_arr empty_sky_light_mask;
  lc_i64_arr empty_block_light_mask;
  lc_u8_grid sky_light;
  lc_u8_grid block_light;
} lc_map_chunk;

#endif /* LIBCHUNK_STRUCT_MAP_CHUNK_H */

#ifndef LIBCHUNK_STRUCT_UPDATE_LIGHT_H
#define LIBCHUNK_STRUCT_UPDATE_LIGHT_H

#include "../types.h"

typedef struct lc_update_light {
  int32_t chunk_x, chunk_z;
  lc_i64_arr sky_light_mask;
  lc_i64_arr block_light_mask;
  lc_i64_arr empty_sky_light_mask;
  lc_i64_arr empty_block_light_mask;
  lc_u8_grid sky_light;
  lc_u8_grid block_light;
} lc_update_light;

#endif /* LIBCHUNK_STRUCT_UPDATE_LIGHT_H */

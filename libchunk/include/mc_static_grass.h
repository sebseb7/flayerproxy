#ifndef MC_STATIC_GRASS_H
#define MC_STATIC_GRASS_H

#include "libchunk.h"

/** Flat grass world: stone below y=63, grass at y=63, air above. Fills *out; caller lc_chunk_free. */
lc_status mc_static_build_grass_chunk(lc_chunk *out, int32_t chunk_x, int32_t chunk_z);

#endif

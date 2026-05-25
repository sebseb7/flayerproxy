#ifndef LIBCHUNK_STRUCT_INITIALIZE_WORLD_BORDER_H
#define LIBCHUNK_STRUCT_INITIALIZE_WORLD_BORDER_H

#include "../types.h"

typedef struct lc_initialize_world_border {
  double x, z;
  double old_diameter, new_diameter;
  int32_t speed;
  int32_t portal_teleport_boundary;
  int32_t warning_blocks;
  int32_t warning_time;
} lc_initialize_world_border;

#endif /* LIBCHUNK_STRUCT_INITIALIZE_WORLD_BORDER_H */

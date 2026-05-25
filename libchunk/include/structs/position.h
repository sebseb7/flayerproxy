#ifndef LIBCHUNK_STRUCT_POSITION_H
#define LIBCHUNK_STRUCT_POSITION_H

#include "../types.h"

typedef struct lc_position {
  int32_t teleport_id;
  double x, y, z;
  double dx, dy, dz;
  float yaw, pitch;
  uint32_t flags;
} lc_position;

#endif /* LIBCHUNK_STRUCT_POSITION_H */

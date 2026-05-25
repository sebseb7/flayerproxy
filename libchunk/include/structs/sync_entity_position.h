#ifndef LIBCHUNK_STRUCT_SYNC_ENTITY_POSITION_H
#define LIBCHUNK_STRUCT_SYNC_ENTITY_POSITION_H

#include "../types.h"

typedef struct lc_sync_entity_position {
  int32_t entity_id;
  double x, y, z;
  double dx, dy, dz;
  float yaw, pitch;
  uint8_t on_ground;
} lc_sync_entity_position;

#endif /* LIBCHUNK_STRUCT_SYNC_ENTITY_POSITION_H */

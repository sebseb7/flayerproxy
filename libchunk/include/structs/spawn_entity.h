#ifndef LIBCHUNK_STRUCT_SPAWN_ENTITY_H
#define LIBCHUNK_STRUCT_SPAWN_ENTITY_H

#include "../types.h"

typedef struct lc_spawn_entity {
  int32_t entity_id;
  lc_uuid object_uuid;
  int32_t type;
  double x, y, z;
  lc_vec3 velocity;
  int8_t pitch, yaw, head_pitch;
  int32_t object_data;
} lc_spawn_entity;

#endif /* LIBCHUNK_STRUCT_SPAWN_ENTITY_H */

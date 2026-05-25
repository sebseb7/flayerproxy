#ifndef LIBCHUNK_STRUCT_ENTITY_VELOCITY_H
#define LIBCHUNK_STRUCT_ENTITY_VELOCITY_H

#include "../types.h"

typedef struct lc_entity_velocity {
  int32_t entity_id;
  lc_vec3 velocity;
} lc_entity_velocity;

#endif /* LIBCHUNK_STRUCT_ENTITY_VELOCITY_H */

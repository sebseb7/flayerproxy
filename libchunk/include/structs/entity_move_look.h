#ifndef LIBCHUNK_STRUCT_ENTITY_MOVE_LOOK_H
#define LIBCHUNK_STRUCT_ENTITY_MOVE_LOOK_H

#include "../types.h"

typedef struct lc_entity_move_look {
  int32_t entity_id;
  int16_t dx, dy, dz;
  int8_t yaw, pitch;
  uint8_t on_ground;
} lc_entity_move_look;

#endif /* LIBCHUNK_STRUCT_ENTITY_MOVE_LOOK_H */

#ifndef LIBCHUNK_STRUCT_REL_ENTITY_MOVE_H
#define LIBCHUNK_STRUCT_REL_ENTITY_MOVE_H

#include "../types.h"

typedef struct lc_rel_entity_move {
  int32_t entity_id;
  int16_t dx, dy, dz;
  uint8_t on_ground;
} lc_rel_entity_move;

#endif /* LIBCHUNK_STRUCT_REL_ENTITY_MOVE_H */

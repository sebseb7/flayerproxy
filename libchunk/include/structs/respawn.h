#ifndef LIBCHUNK_STRUCT_RESPAWN_H
#define LIBCHUNK_STRUCT_RESPAWN_H

#include "spawn_info.h"

typedef struct lc_respawn {
  lc_spawn_info world_state;
  uint8_t copy_metadata;
} lc_respawn;

#endif /* LIBCHUNK_STRUCT_RESPAWN_H */

#ifndef LIBCHUNK_STRUCT_SPAWN_INFO_H
#define LIBCHUNK_STRUCT_SPAWN_INFO_H

#include "../types.h"

typedef struct lc_spawn_info {
  int32_t dimension;
  char *name;
  int64_t hashed_seed;
  int8_t gamemode; /* 0=survival .. */
  uint8_t previous_gamemode;
  uint8_t is_debug;
  uint8_t is_flat;
  uint8_t has_death;
  char *death_dimension_name;
  lc_block_pos death_pos;
  int32_t portal_cooldown;
  int32_t sea_level;
} lc_spawn_info;

#endif /* LIBCHUNK_STRUCT_SPAWN_INFO_H */

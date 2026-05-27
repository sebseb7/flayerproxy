#ifndef LIBCHUNK_STRUCT_PLAY_LOGIN_H
#define LIBCHUNK_STRUCT_PLAY_LOGIN_H

#include "spawn_info.h"

typedef struct lc_play_login {
  int32_t entity_id;
  uint8_t hardcore;
  const char **world_names;
  size_t world_name_count;
  int32_t max_players;
  int32_t view_distance;
  int32_t simulation_distance;
  uint8_t reduced_debug_info;
  uint8_t enable_respawn_screen;
  uint8_t do_limited_crafting;
  lc_spawn_info world_state;
  uint8_t enforces_secure_chat;
} lc_play_login;

#endif /* LIBCHUNK_STRUCT_PLAY_LOGIN_H */

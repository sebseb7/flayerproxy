#ifndef MC_REGISTRY_JOIN_TEMPLATE_H
#define MC_REGISTRY_JOIN_TEMPLATE_H

#include <stddef.h>
#include <stdint.h>

#include "libchunk.h"
#include "packets_write.h"

/** Parsed play-join fields from upstream (no raw packet bytes). */
typedef struct mc_registry_join_template {
  int login_valid;
  uint8_t login_hardcore;
  char **login_world_names;
  size_t login_world_name_count;
  int32_t login_max_players;
  int32_t login_view_distance;
  int32_t login_simulation_distance;
  uint8_t login_reduced_debug_info;
  uint8_t login_enable_respawn_screen;
  uint8_t login_do_limited_crafting;
  lc_spawn_info login_world_state;
  uint8_t login_enforces_secure_chat;

  int position_valid;
  lc_position position;

  int world_border_valid;
  lc_initialize_world_border world_border;

  int update_time_valid;
  int64_t time_world_age;
  int64_t time_time_of_day;
  uint8_t time_tick_day_time;

  int spawn_position_valid;
  char *spawn_dimension;
  lc_block_pos spawn_pos;
  float spawn_yaw;
  float spawn_pitch;
} mc_registry_join_template;

void mc_registry_join_template_clear(mc_registry_join_template *t);
void mc_registry_join_template_free(mc_registry_join_template *t);

#endif

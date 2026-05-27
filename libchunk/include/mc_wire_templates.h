#ifndef MC_WIRE_TEMPLATES_H
#define MC_WIRE_TEMPLATES_H

#include <stddef.h>
#include <stdint.h>

#include "mc_server_common.h"

/** Server view distance in login / set_chunk_cache_radius (reference Java sends 3 with server.properties view-distance=2). */
#define MC_STATIC_VIEW_DISTANCE 3
/** Chunk grid radius around spawn; reference join sends 9×9 (radius 4). */
#define MC_STATIC_CHUNK_RADIUS 4
#define MC_STATIC_SIM_DISTANCE 2
#define MC_STATIC_SPAWN_X 8
#define MC_STATIC_SPAWN_Y 64
#define MC_STATIC_SPAWN_Z 8
#define MC_STATIC_SPAWN_CHUNK_X 0
#define MC_STATIC_SPAWN_CHUNK_Z 0

typedef struct mc_patch_ctx {
  int32_t entity_id;
  const uint8_t *uuid;
  const char *username;
  int32_t chunk_x;
  int32_t chunk_z;
  mc_gamemode gamemode;
  int32_t teleport_id;
  double spawn_x;
  double spawn_y;
  double spawn_z;
} mc_patch_ctx;

typedef struct mc_server_world {
  int32_t view_radius;
  double spawn_x;
  double spawn_y;
  double spawn_z;
  int32_t spawn_chunk_x;
  int32_t spawn_chunk_z;
} mc_server_world;

int mc_templates_init(void);
void mc_templates_free(void);
const mc_server_world *mc_templates_world(void);

int mc_template_send_config_sequence(int fd, const mc_patch_ctx *ctx);
int mc_template_send_play_join(int fd, const mc_patch_ctx *ctx);
int mc_template_send_grass_world(int fd, const mc_patch_ctx *ctx);
int mc_template_send_update_view_position(int fd, int32_t chunk_x, int32_t chunk_z);
int mc_template_send_map_chunk_at(int fd, int32_t chunk_x, int32_t chunk_z);
int mc_template_send_unload_chunk_at(int fd, int32_t chunk_x, int32_t chunk_z);

/** map_chunk wire body (varint id + payload) for spectator. Caller frees *wire. */
int mc_templates_grass_packet_wire(uint8_t **wire, size_t *wire_len);

#endif

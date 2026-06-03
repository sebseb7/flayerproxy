#ifndef MC_REGISTRY_CAPTURE_H
#define MC_REGISTRY_CAPTURE_H

#include <stddef.h>
#include <stdint.h>

#include "mc_registry_join_template.h"

/** Mirror S2C config packet to a connected downstream client (optional). */
typedef int (*mc_registry_mirror_config_fn)(void *ctx, int32_t pkt_id, const uint8_t *body, size_t body_len);
/** Block until downstream client sends C2S select_known_packs (optional). */
typedef int (*mc_registry_wait_client_packs_fn)(void *ctx, uint8_t **packs, size_t *packs_len);
typedef void (*mc_registry_release_client_packs_fn)(void *ctx, uint8_t *packs);

/** Upstream Minecraft server to mirror configuration registries from. */
typedef struct mc_registry_capture_config {
  const char *host;
  int port;
  const char *username;
  /** Client C2S select_known_packs payload to negotiate on upstream (optional). */
  const uint8_t *client_known_packs;
  size_t client_known_packs_len;
  /** When set, config is streamed to downstream in lockstep with upstream. */
  void *downstream_ctx;
  mc_registry_mirror_config_fn mirror_config_s2c;
  mc_registry_wait_client_packs_fn wait_client_select_known_packs;
  mc_registry_release_client_packs_fn release_client_packs;
} mc_registry_capture_config;

typedef enum {
  MC_REG_SYNC_REGISTRY = 0,
  MC_REG_SYNC_TAGS = 1,
  MC_REG_SYNC_RESET_CHAT = 2,
  MC_REG_SYNC_SELECT_KNOWN_PACKS = 3,
  MC_REG_SYNC_PLAY = 4,
} mc_reg_sync_kind;

/** One server→client packet captured from --registry-from for replay. */
typedef struct mc_reg_sync_step {
  mc_reg_sync_kind kind;
  int32_t pkt_id;
  char label[64];
  uint8_t *data;
  size_t len;
} mc_reg_sync_step;

typedef struct mc_registry_capture_result {
  mc_reg_sync_step *steps;
  size_t step_count;
  mc_registry_join_template join;
} mc_registry_capture_result;

/** Called once config (registry_data + update_tags) is captured; fetch may continue for play join. */
typedef void (*mc_registry_config_ready_fn)(const mc_registry_capture_result *config, int ok, void *ctx);

/** Play join fields captured; fetch continues for map_chunk cache. */
typedef void (*mc_registry_play_join_ready_fn)(const mc_registry_capture_result *cap, void *ctx);

/** Progress during play capture (e.g. map_chunk stored); wake threads waiting on cache. */
typedef void (*mc_registry_capture_wake_fn)(void *ctx);

/**
 * Connect as a minimal client, complete configuration (registry_data, update_tags,
 * select_known_packs), enter play, and capture join payloads (recipes, world border, time).
 * When on_config_ready is set, it is invoked after config finish (before play capture).
 * on_play_join_ready / on_capture_wake are optional and only used with on_config_ready.
 */
int mc_registry_capture_configuration(const mc_registry_capture_config *cfg, mc_registry_capture_result *out,
                                      mc_registry_config_ready_fn on_config_ready,
                                      mc_registry_play_join_ready_fn on_play_join_ready,
                                      mc_registry_capture_wake_fn on_capture_wake, void *ctx);

void mc_registry_capture_result_free(mc_registry_capture_result *out);

#endif

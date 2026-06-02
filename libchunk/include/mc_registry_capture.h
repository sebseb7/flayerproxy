#ifndef MC_REGISTRY_CAPTURE_H
#define MC_REGISTRY_CAPTURE_H

#include <stddef.h>
#include <stdint.h>

/** Upstream Minecraft server to mirror configuration registries from. */
typedef struct mc_registry_capture_config {
  const char *host;
  int port;
  const char *username;
  /** Client C2S select_known_packs payload to negotiate on upstream (optional). */
  const uint8_t *client_known_packs;
  size_t client_known_packs_len;
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
} mc_registry_capture_result;

/** Called once config (registry_data + update_tags) is captured; fetch may continue for play join. */
typedef void (*mc_registry_config_ready_fn)(const mc_registry_capture_result *config, int ok, void *ctx);

/**
 * Connect as a minimal client, complete configuration (registry_data, update_tags,
 * select_known_packs), enter play, and capture join payloads (recipes, world border, time).
 * When on_config_ready is set, it is invoked after config finish (before play capture).
 */
int mc_registry_capture_configuration(const mc_registry_capture_config *cfg, mc_registry_capture_result *out,
                                      mc_registry_config_ready_fn on_config_ready, void *ctx);

void mc_registry_capture_result_free(mc_registry_capture_result *out);

#endif

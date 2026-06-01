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
} mc_reg_sync_kind;

/** One server→client configuration packet to replay during registry sync. */
typedef struct mc_reg_sync_step {
  mc_reg_sync_kind kind;
  char label[64];
  uint8_t *data;
  size_t len;
} mc_reg_sync_step;

typedef struct mc_registry_capture_result {
  mc_reg_sync_step *steps;
  size_t step_count;
} mc_registry_capture_result;

/**
 * Connect as a minimal client, complete configuration through registry_data +
 * update_tags, and return payloads in wire send order (no finish_configuration).
 */
int mc_registry_capture_configuration(const mc_registry_capture_config *cfg, mc_registry_capture_result *out);

void mc_registry_capture_result_free(mc_registry_capture_result *out);

#endif

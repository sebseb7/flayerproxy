#ifndef MC_STATIC_REGISTRIES_H
#define MC_STATIC_REGISTRIES_H

#include <stddef.h>
#include <stdint.h>

#include "packets_write.h"

/** When set before init, first client join loads registries from this server (then cached). */
typedef struct mc_static_registry_fetch {
  const char *host;
  int port;
  const char *username;
} mc_static_registry_fetch;

/** NULL host disables remote fetch (use mc_static_registries_data.c on first join). */
void mc_static_registries_set_fetch(const mc_static_registry_fetch *fetch);

/** registry-from: connect upstream on C2S login_acknowledged (once per cache cycle). */
void mc_static_registries_start_fetch_on_login_acknowledged(void);

/** Reset cache state. */
int mc_static_registries_init(void);
void mc_static_registries_free(void);

/** 1 when config registries (and select_known_packs) are cached from upstream or embedded. */
int mc_static_registries_config_cached(void);

/** 1 when play join + chunk cache from upstream is complete. */
int mc_static_registries_play_ready(void);

/** Cached select_known_packs payload from last successful fetch, or NULL. */
const uint8_t *mc_static_cached_select_known_packs(size_t *len);

/** Cached play payload by packet id from last successful fetch, or NULL. */
const uint8_t *mc_static_cached_play_payload(int32_t pkt_id, size_t *len);

/** Replay cached update_recipes / recipe_book_settings / recipe_book_add (skips missing). */
int mc_static_send_cached_recipe_burst(int fd);

/** Block until upstream play join + chunks are cached (registry-from only). */
void mc_static_wait_play_cache(void);

/** Fill play login from upstream cache; entity_id is the downstream client id. */
int mc_static_fill_join_login(lc_play_login *login, int32_t entity_id);

/** Fill synchronize-player-position from upstream cache; teleport_id is downstream. */
int mc_static_fill_join_position(lc_position *pos, int32_t teleport_id);

/**
 * After client select_known_packs: load cache for this pack list if needed,
 * then registry_data, update_tags, finish.
 */
int mc_static_send_registry_sync(int fd, const uint8_t *client_known_packs, size_t client_known_packs_len);

/** Number of registry_data packets in the current sync set. */
size_t mc_static_registry_count(void);

#endif

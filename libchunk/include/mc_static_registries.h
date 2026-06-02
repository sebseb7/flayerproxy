#ifndef MC_STATIC_REGISTRIES_H
#define MC_STATIC_REGISTRIES_H

#include <stddef.h>
#include <stdint.h>

/** When set before init, first client join loads registries from this server (then cached). */
typedef struct mc_static_registry_fetch {
  const char *host;
  int port;
  const char *username;
} mc_static_registry_fetch;

/** NULL host disables remote fetch (use mc_static_registries_data.c on first join). */
void mc_static_registries_set_fetch(const mc_static_registry_fetch *fetch);

/** Reset cache state; does not load registries until a client needs them. */
int mc_static_registries_init(void);
void mc_static_registries_free(void);

/** Cached select_known_packs payload from last successful fetch, or NULL. */
const uint8_t *mc_static_cached_select_known_packs(size_t *len);

/** Cached play payload by packet id from last successful fetch, or NULL. */
const uint8_t *mc_static_cached_play_payload(int32_t pkt_id, size_t *len);

/** Replay cached update_recipes / recipe_book_settings / recipe_book_add (skips missing). */
int mc_static_send_cached_recipe_burst(int fd);

/** Wait for background play join fetch when config is already cached. */
void mc_static_wait_play_cache(void);

/**
 * After client select_known_packs: load cache for this pack list if needed,
 * then registry_data, update_tags, finish.
 */
int mc_static_send_registry_sync(int fd, const uint8_t *client_known_packs, size_t client_known_packs_len);

/** Number of registry_data packets in the current sync set. */
size_t mc_static_registry_count(void);

#endif

#ifndef LIBCHUNK_MAP_COLORS_H
#define LIBCHUNK_MAP_COLORS_H

#include <stdint.h>

typedef struct lc_chunk lc_chunk;

/** Minecraft 1.21.4 map protocol color count (IDs 0–247). */
#define LC_MAP_PROTOCOL_COLORS 248

/** RGB for protocol map color id (0–247). Transparent (0–3) is black. */
void lc_map_protocol_rgb(uint8_t protocol_id, uint8_t *r, uint8_t *g, uint8_t *b);

/** Max global block state id (minecraft-data 1.21.10 — regenerate via generate-state-map-colors.js). */
#define LC_STATE_MAP_MAX 29670

/** Top-face texture RGB from minecraft-assets (generate-state-top-texture-colors.js). */
void lc_state_id_top_rgb(int32_t state_id, uint8_t *r, uint8_t *g, uint8_t *b);

/** Non-zero for water / bubble_column (transparent in map PNGs). */
int lc_state_id_is_water(int32_t state_id);

/** Map global block state id → protocol map color id; 255 if unknown/air. */
uint8_t lc_state_id_map_protocol(int32_t state_id);

/** Human-readable block state (minecraft-data 1.21.10); "?" if unknown. */
const char *lc_state_id_block_name(int32_t state_id);

/** Registry biome id at block column (defaults to plains when section has no biomes). */
int32_t lc_chunk_biome_at(const lc_chunk *c, int lx, int32_t world_y, int lz);

/** Re-tint baked top RGB for grass (kind 1) / foliage (kind 2) using column biome. */
void lc_map_rgb_apply_biome_tint(int32_t state_id, int32_t biome_id, uint8_t *r, uint8_t *g, uint8_t *b);

#endif

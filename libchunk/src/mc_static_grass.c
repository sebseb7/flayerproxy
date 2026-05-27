#include "mc_static_grass.h"

#include "internal.h"

#include <stdlib.h>
#include <string.h>

#define MC_GRASS_SURFACE_Y 63
#define MC_GRASS_PLAINS_BIOME 40
#define MC_GRASS_STONE 1
#define MC_GRASS_GRASS 9
#define MC_GRASS_AIR 0
#define MC_SECTION_LIGHT_SIZE 2048

static int grass_block_index(int lx, int ly, int lz) { return (ly << 8) | (lz << 4) | lx; }

static void mask_set(lc_i64_arr *mask, int bit) {
  if (bit < 0) return;
  size_t need = (size_t)(bit / 64) + 1;
  if (mask->count < need) {
    int64_t *vals = (int64_t *)realloc(mask->values, need * sizeof(int64_t));
    if (!vals) return;
    for (size_t i = mask->count; i < need; i++) vals[i] = 0;
    mask->values = vals;
    mask->count = need;
  }
  mask->values[bit / 64] |= (int64_t)1 << (bit % 64);
}

/** LevelLightEngine.getMinLightSection(): minSectionY - 1. */
static int get_min_light_section(int32_t min_y) { return (min_y >> 4) - 1; }

/** Light mask bit i maps to section Y = getMinLightSection() + i. */
static int light_bit_for_section_y(int32_t min_y, int32_t section_y) {
  return section_y - get_min_light_section(min_y);
}

static int section_is_air(const lc_chunk_section *sec) {
  for (int i = 0; i < LC_BLOCK_VOLUME; i++) {
    if (sec->state_ids[i] != MC_GRASS_AIR) return 0;
  }
  return 1;
}

static lc_status append_light_row(lc_u8_grid *grid, const uint8_t *data, size_t len) {
  size_t row = grid->row_count;
  uint8_t **rows = (uint8_t **)realloc(grid->rows, (row + 1) * sizeof(uint8_t *));
  size_t *lens = (size_t *)realloc(grid->row_lens, (row + 1) * sizeof(size_t));
  if (!rows || !lens) {
    free(rows);
    free(lens);
    return LC_ERR_OOM;
  }
  grid->rows = rows;
  grid->row_lens = lens;
  grid->rows[row] = (uint8_t *)malloc(len);
  if (!grid->rows[row]) return LC_ERR_OOM;
  memcpy(grid->rows[row], data, len);
  grid->row_lens[row] = len;
  grid->row_count = row + 1;
  return LC_OK;
}

static lc_status mc_static_apply_chunk_light(lc_chunk *c) {
  const int surface_section_y = MC_GRASS_SURFACE_Y >> 4;
  /* Vanilla stores sky light in the air section above the top solid block. */
  const int sky_light_section_y = surface_section_y + 1;
  const int sky_light_bit = light_bit_for_section_y(c->min_y, sky_light_section_y);
  uint8_t sky[MC_SECTION_LIGHT_SIZE];

  for (int bit = 0; bit < sky_light_bit; bit++) {
    mask_set(&c->empty_sky_light_mask, bit);
    mask_set(&c->empty_block_light_mask, bit);
  }
  mask_set(&c->empty_block_light_mask, sky_light_bit);

  lc_chunk_section *sec = NULL;
  for (size_t i = 0; i < c->section_count; i++) {
    if (c->sections[i].section_y == sky_light_section_y) {
      sec = &c->sections[i];
      break;
    }
  }
  if (!sec || !section_is_air(sec)) return LC_OK;

  memset(sky, 0xFF, sizeof sky);
  mask_set(&c->sky_light_mask, sky_light_bit);
  if (append_light_row(&c->sky_light, sky, sizeof sky) != LC_OK) return LC_ERR_OOM;

  return LC_OK;
}

static int16_t grass_count_solid(const int32_t *state_ids) {
  int n = 0;
  for (int i = 0; i < LC_BLOCK_VOLUME; i++) {
    if (state_ids[i] != 0) n++;
  }
  return (int16_t)n;
}

static lc_status lc_chunk_add_section_blocks(lc_chunk *c, int32_t section_y, int32_t fill_sid) {
  lc_chunk_section *next =
      (lc_chunk_section *)realloc(c->sections, (c->section_count + 1) * sizeof(lc_chunk_section));
  if (!next) return LC_ERR_OOM;
  c->sections = next;
  lc_chunk_section *sec = &c->sections[c->section_count++];
  memset(sec, 0, sizeof *sec);
  sec->section_y = section_y;
  for (int i = 0; i < LC_BLOCK_VOLUME; i++) sec->state_ids[i] = fill_sid;
  sec->solid_block_count = grass_count_solid(sec->state_ids);
  for (int i = 0; i < LC_BIOME_VOLUME; i++) sec->biome_ids[i] = MC_GRASS_PLAINS_BIOME;
  sec->has_biomes = 1;
  return LC_OK;
}

lc_status mc_static_build_grass_chunk(lc_chunk *out, int32_t chunk_x, int32_t chunk_z) {
  if (!out) return LC_ERR_INVALID;
  lc_chunk_init(out);
  out->x = chunk_x;
  out->z = chunk_z;
  out->min_y = LC_CHUNK_DEFAULT_MIN_Y;
  out->world_height = LC_CHUNK_DEFAULT_WORLD_HEIGHT;

  const int32_t surface_section = MC_GRASS_SURFACE_Y >> 4;
  const int32_t first_section = out->min_y >> 4;

  for (int32_t sy = first_section; sy < surface_section; sy++) {
    if (lc_chunk_add_section_blocks(out, sy, MC_GRASS_STONE) != LC_OK) goto fail;
  }

  if (lc_chunk_add_section_blocks(out, surface_section, MC_GRASS_STONE) != LC_OK) goto fail;
  {
    lc_chunk_section *sec = &out->sections[out->section_count - 1];
    const int32_t base_y = surface_section << 4;
    const int32_t local_top = MC_GRASS_SURFACE_Y - base_y;
    for (int ly = 0; ly < local_top; ly++) {
      for (int lz = 0; lz < 16; lz++) {
        for (int lx = 0; lx < 16; lx++) {
          sec->state_ids[grass_block_index(lx, ly, lz)] = MC_GRASS_STONE;
        }
      }
    }
    for (int lz = 0; lz < 16; lz++) {
      for (int lx = 0; lx < 16; lx++) {
        sec->state_ids[grass_block_index(lx, local_top, lz)] = MC_GRASS_GRASS;
      }
    }
    sec->solid_block_count = grass_count_solid(sec->state_ids);
  }

  if (lc_chunk_add_section_blocks(out, surface_section + 1, MC_GRASS_AIR) != LC_OK) goto fail;

  if (lc_chunk_build_heightmaps(out) != LC_OK) goto fail;
  if (mc_static_apply_chunk_light(out) != LC_OK) goto fail;

  return LC_OK;
fail:
  lc_chunk_free(out);
  return LC_ERR_OOM;
}

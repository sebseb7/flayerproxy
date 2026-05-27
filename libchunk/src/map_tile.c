#include "internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TILE_CHUNKS LC_MAP_TILE_CHUNKS_PER_SIDE
#define CHUNK_PX LC_MAP_CHUNK_PNG_SIZE
#define TILE_PX LC_MAP_TILE_PNG_SIZE
#define LC_MAP_TILE_DIR_SIZE 4096
/** Room for "/x<world>_z<world>.avif" after sheet->dir (GCC -Wformat-truncation). */
#define LC_MAP_TILE_PATH_SUFFIX 64
#define LC_MAP_TILE_PATH_MAX (LC_MAP_TILE_DIR_SIZE + LC_MAP_TILE_PATH_SUFFIX)

typedef struct lc_map_tile_slot {
  int32_t tile_chunk_x;
  int32_t tile_chunk_z;
  int32_t tile_world_x;
  int32_t tile_world_z;
  uint8_t *rgb;
  int loaded;
} lc_map_tile_slot;

struct lc_map_tile_sheet {
  char dir[LC_MAP_TILE_DIR_SIZE];
  lc_map_tile_slot *slots;
  size_t slot_count;
  size_t slot_cap;
};
/* Good for: Chunk (x,z) at corner of 16×16 mega-tile.
 * Callers: map_tile.c (same file).
 */

static int32_t lc_tile_origin_chunk(int32_t c) {
  if (c >= 0) return (c / TILE_CHUNKS) * TILE_CHUNKS;
  return ((c - (TILE_CHUNKS - 1)) / TILE_CHUNKS) * TILE_CHUNKS;
}

static void lc_tile_path(const lc_map_tile_sheet *sheet, int32_t twx, int32_t twz, char *buf,
                         size_t buflen) {
  if (buflen == 0) return;
  snprintf(buf, buflen, "%s/x%d_z%d.avif", sheet->dir, twx, twz);
}

static lc_map_tile_slot *lc_tile_slot_get(lc_map_tile_sheet *sheet, int32_t tcx, int32_t tcz) {
  for (size_t i = 0; i < sheet->slot_count; i++) {
    if (sheet->slots[i].tile_chunk_x == tcx && sheet->slots[i].tile_chunk_z == tcz) {
      return &sheet->slots[i];
    }
  }

  if (sheet->slot_count == sheet->slot_cap) {
    size_t ncap = sheet->slot_cap ? sheet->slot_cap * 2 : 4;
    lc_map_tile_slot *next =
        (lc_map_tile_slot *)realloc(sheet->slots, ncap * sizeof(lc_map_tile_slot));
    if (!next) return NULL;
    sheet->slots = next;
    sheet->slot_cap = ncap;
  }

  lc_map_tile_slot *slot = &sheet->slots[sheet->slot_count++];
  memset(slot, 0, sizeof(*slot));
  slot->tile_chunk_x = tcx;
  slot->tile_chunk_z = tcz;
  slot->tile_world_x = tcx * 16;
  slot->tile_world_z = tcz * 16;
  return slot;
}
/* Good for: Load or allocate RGB buffer for one tile slot.
 * Callers: map_tile.c (same file).
 */

static lc_status lc_tile_slot_ensure_rgb(lc_map_tile_sheet *sheet, lc_map_tile_slot *slot) {
  if (slot->loaded) return LC_OK;

  size_t px = (size_t)TILE_PX * (size_t)TILE_PX * 3;
  slot->rgb = (uint8_t *)malloc(px);
  if (!slot->rgb) return LC_ERR_OOM;

  char path[LC_MAP_TILE_PATH_MAX];
  lc_tile_path(sheet, slot->tile_world_x, slot->tile_world_z, path, sizeof path);

  int w = 0, h = 0;
  uint8_t *existing = NULL;
  if (lc_read_rgb_avif(path, &existing, &w, &h) == LC_OK) {
    if (w == TILE_PX && h == TILE_PX) {
      memcpy(slot->rgb, existing, px);
      free(existing);
      slot->loaded = 1;
      return LC_OK;
    }
    free(existing);
  }

  memset(slot->rgb, 0, px);
  slot->loaded = 1;
  return LC_OK;
}
/* Good for: Write mega-tile image to disk.
 * Callers: map_tile.c (same file).
 */

static lc_status lc_tile_slot_save(const lc_map_tile_sheet *sheet, const lc_map_tile_slot *slot) {
  char path[LC_MAP_TILE_PATH_MAX];
  lc_tile_path(sheet, slot->tile_world_x, slot->tile_world_z, path, sizeof path);
  return lc_write_rgb_avif(path, slot->rgb, TILE_PX, TILE_PX);
}

lc_map_tile_sheet *lc_map_tile_sheet_open(const char *x16_dir) {
  if (!x16_dir || !x16_dir[0]) return NULL;
  lc_map_tile_sheet *sheet = (lc_map_tile_sheet *)calloc(1, sizeof(*sheet));
  if (!sheet) return NULL;
  snprintf(sheet->dir, sizeof sheet->dir, "%.*s",
           (int)(sizeof sheet->dir - LC_MAP_TILE_PATH_SUFFIX), x16_dir);
  return sheet;
}
/* Good for: 16×16-chunk mega-tile PNG stitching for map captures.
 * Callers: libchunk.h (public API, no .c callers in tree).
 */

void lc_map_tile_sheet_close(lc_map_tile_sheet *sheet) {
  if (!sheet) return;
  for (size_t i = 0; i < sheet->slot_count; i++) {
    free(sheet->slots[i].rgb);
  }
  free(sheet->slots);
  free(sheet);
}
/* Good for: 16×16-chunk mega-tile PNG stitching for map captures.
 * Callers: libchunk.h (public API, no .c callers in tree).
 */

lc_status lc_map_tile_sheet_blend_map_chunk(lc_map_tile_sheet *sheet, const lc_map_chunk *mc) {
  if (!sheet || !mc) return LC_ERR_INVALID;

  int32_t tcx = lc_tile_origin_chunk(mc->x);
  int32_t tcz = lc_tile_origin_chunk(mc->z);
  lc_map_tile_slot *slot = lc_tile_slot_get(sheet, tcx, tcz);
  if (!slot) return LC_ERR_OOM;

  lc_status st = lc_tile_slot_ensure_rgb(sheet, slot);
  if (st != LC_OK) return st;

  lc_chunk c;
  lc_chunk_init(&c);
  st = lc_chunk_from_map_chunk(mc, &c);
  if (st != LC_OK) {
    lc_chunk_free(&c);
    return st;
  }

  uint8_t *chunk_rgb = (uint8_t *)malloc((size_t)CHUNK_PX * (size_t)CHUNK_PX * 3);
  if (!chunk_rgb) {
    lc_chunk_free(&c);
    return LC_ERR_OOM;
  }

  st = lc_chunk_render_top_rgb(&c, chunk_rgb);
  lc_chunk_free(&c);
  if (st != LC_OK) {
    free(chunk_rgb);
    return st;
  }

  int local_cx = mc->x - tcx;
  int local_cz = mc->z - tcz;
  if (local_cx < 0 || local_cx >= TILE_CHUNKS || local_cz < 0 || local_cz >= TILE_CHUNKS) {
    free(chunk_rgb);
    return LC_ERR_INVALID;
  }

  int dst_x0 = local_cx * CHUNK_PX;
  int dst_z0 = local_cz * CHUNK_PX;

  for (int row = 0; row < CHUNK_PX; row++) {
    const uint8_t *src = chunk_rgb + (size_t)row * (size_t)CHUNK_PX * 3;
    uint8_t *dst = slot->rgb + ((size_t)(dst_z0 + row) * (size_t)TILE_PX + (size_t)dst_x0) * 3;
    memcpy(dst, src, (size_t)CHUNK_PX * 3);
  }

  free(chunk_rgb);

  return lc_tile_slot_save(sheet, slot);
}

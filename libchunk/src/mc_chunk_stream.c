#include "mc_chunk_stream.h"

#include "mc_log.h"
#include "mc_wire_templates.h"

#include <math.h>
#include <string.h>
/* Good for: Test whether chunk (x,z) is in the loaded set.
 * Callers: mc_chunk_stream.c (same file).
 */

static int chunk_loaded(const mc_chunk_stream *cs, int32_t x, int32_t z) {
  for (int i = 0; i < cs->loaded_count; i++) {
    if (cs->loaded[i].x == x && cs->loaded[i].z == z) return 1;
  }
  return 0;
}
/* Good for: Add chunk (x,z) to loaded set; 0 on success, -1 if table full.
 * Callers: mc_chunk_stream.c (same file).
 */

static int chunk_mark_loaded(mc_chunk_stream *cs, int32_t x, int32_t z) {
  if (chunk_loaded(cs, x, z)) return 0;
  if (cs->loaded_count >= MC_CHUNK_STREAM_MAX) return -1;
  cs->loaded[cs->loaded_count].x = x;
  cs->loaded[cs->loaded_count].z = z;
  cs->loaded_count++;
  return 0;
}
/* Good for: Remove chunk (x,z) from loaded set.
 * Callers: mc_chunk_stream.c (same file).
 */

static void chunk_unmark_loaded(mc_chunk_stream *cs, int32_t x, int32_t z) {
  for (int i = 0; i < cs->loaded_count; i++) {
    if (cs->loaded[i].x == x && cs->loaded[i].z == z) {
      cs->loaded[i] = cs->loaded[cs->loaded_count - 1];
      cs->loaded_count--;
      return;
    }
  }
}
/* Good for: Convert block coords to chunk coords (floor div 16).
 * Callers: mc_chunk_stream.c (same file).
 */

static void block_to_chunk(double x, double z, int32_t *cx, int32_t *cz) {
  *cx = (int32_t)floor(x / 16.0);
  *cz = (int32_t)floor(z / 16.0);
}
/* Good for: Initialize chunk-stream state and view radius.
 * Callers: mc_static_server.c.
 */

void mc_chunk_stream_init(mc_chunk_stream *cs, int32_t radius) {
  memset(cs, 0, sizeof *cs);
  cs->radius = radius;
}
/* Good for: Mark all chunks in radius around center as loaded (no wire yet).
 * Callers: mc_static_server.c.
 */

void mc_chunk_stream_mark_grid(mc_chunk_stream *cs, int32_t center_cx, int32_t center_cz) {
  cs->view_cx = center_cx;
  cs->view_cz = center_cz;
  cs->loaded_count = 0;
  for (int dx = -cs->radius; dx <= cs->radius; dx++) {
    for (int dz = -cs->radius; dz <= cs->radius; dz++) {
      (void)chunk_mark_loaded(cs, center_cx + dx, center_cz + dz);
    }
  }
}
/* Good for: Send map_chunk / unload for chunks entering or leaving view radius.
 * Callers: mc_chunk_stream.c (same file).
 */

static int sync_view_chunks(mc_chunk_stream *cs, int fd, int32_t center_cx, int32_t center_cz) {
  int32_t to_load_x[MC_CHUNK_STREAM_MAX];
  int32_t to_load_z[MC_CHUNK_STREAM_MAX];
  int to_load_count = 0;

  int32_t to_unload_x[MC_CHUNK_STREAM_MAX];
  int32_t to_unload_z[MC_CHUNK_STREAM_MAX];
  int to_unload_count = 0;

  for (int dx = -cs->radius; dx <= cs->radius; dx++) {
    for (int dz = -cs->radius; dz <= cs->radius; dz++) {
      int32_t x = center_cx + dx;
      int32_t z = center_cz + dz;
      if (!chunk_loaded(cs, x, z)) {
        if (to_load_count >= MC_CHUNK_STREAM_MAX) return -1;
        to_load_x[to_load_count] = x;
        to_load_z[to_load_count] = z;
        to_load_count++;
      }
    }
  }

  for (int i = 0; i < cs->loaded_count; i++) {
    int32_t x = cs->loaded[i].x;
    int32_t z = cs->loaded[i].z;
    if (x < center_cx - cs->radius || x > center_cx + cs->radius || z < center_cz - cs->radius ||
        z > center_cz + cs->radius) {
      if (to_unload_count >= MC_CHUNK_STREAM_MAX) return -1;
      to_unload_x[to_unload_count] = x;
      to_unload_z[to_unload_count] = z;
      to_unload_count++;
    }
  }

  if (to_unload_count == 0 && to_load_count == 0) return 0;

  if (mc_template_send_update_view_position(fd, center_cx, center_cz) != 0) return -1;

  for (int i = 0; i < to_unload_count; i++) {
    if (mc_template_send_unload_chunk_at(fd, to_unload_x[i], to_unload_z[i]) != 0) return -1;
    chunk_unmark_loaded(cs, to_unload_x[i], to_unload_z[i]);
    MC_LOGI("static_server", "unload_chunk (%d,%d)", to_unload_x[i], to_unload_z[i]);
  }

  if (to_load_count > 0) {
    MC_LOGI("static_server", "streaming %d chunk(s) around (%d,%d)", to_load_count, center_cx, center_cz);
    for (int i = 0; i < to_load_count; i++) {
      if (mc_template_send_map_chunk_at(fd, to_load_x[i], to_load_z[i]) != 0) return -1;
      if (chunk_mark_loaded(cs, to_load_x[i], to_load_z[i]) != 0) return -1;
    }
  }

  cs->view_cx = center_cx;
  cs->view_cz = center_cz;
  return 0;
}
/* Good for: Query whether (cx,cz) is in the loaded set.
 * Callers: mc_static_server.c.
 */

int mc_chunk_stream_has_chunk(const mc_chunk_stream *cs, int32_t cx, int32_t cz) {
  return chunk_loaded(cs, cx, cz);
}
/* Good for: On player move, sync map_chunk/unload when chunk coords change.
 * Callers: mc_static_server.c.
 */

int mc_chunk_stream_on_move(mc_chunk_stream *cs, int fd, double x, double y, double z) {
  cs->pos_x = x;
  cs->pos_y = y;
  cs->pos_z = z;
  cs->has_pos = 1;

  int32_t cx, cz;
  block_to_chunk(x, z, &cx, &cz);
  if (cx == cs->view_cx && cz == cs->view_cz) return 0;
  return sync_view_chunks(cs, fd, cx, cz);
}

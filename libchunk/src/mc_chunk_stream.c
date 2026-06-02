#include "mc_chunk_stream.h"

#include "libchunk.h"
#include "mc_log.h"
#include "mc_packet_ids.h"
#include "mc_server_common.h"
#include "mc_wire.h"
#include "mc_wire_templates.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define MC_STATIC_CHUNKS_MAX 512

typedef struct mc_static_chunk_entry {
  int32_t x;
  int32_t z;
  uint8_t *data;
  size_t len;
} mc_static_chunk_entry;

static mc_static_chunk_entry g_upstream_entries[MC_STATIC_CHUNKS_MAX];
static int g_upstream_entry_count;
static int g_upstream_mode;

static int upstream_find_index(int32_t x, int32_t z) {
  for (int i = 0; i < g_upstream_entry_count; i++) {
    if (g_upstream_entries[i].x == x && g_upstream_entries[i].z == z) return i;
  }
  return -1;
}

void mc_static_chunks_set_upstream(int enabled) { g_upstream_mode = enabled ? 1 : 0; }

int mc_static_chunks_upstream(void) { return g_upstream_mode; }

void mc_static_chunks_clear(void) {
  for (int i = 0; i < g_upstream_entry_count; i++) free(g_upstream_entries[i].data);
  g_upstream_entry_count = 0;
}

size_t mc_static_chunks_count(void) { return (size_t)g_upstream_entry_count; }

int32_t mc_static_chunk_radius_from_view(int32_t view_distance) {
  if (view_distance <= 0) return 0;
  return view_distance - 1;
}

int32_t mc_static_chunks_max_cached_radius(int32_t cx, int32_t cz) {
  int32_t max_r = 0;
  for (int i = 0; i < g_upstream_entry_count; i++) {
    int32_t dx = g_upstream_entries[i].x - cx;
    if (dx < 0) dx = -dx;
    int32_t dz = g_upstream_entries[i].z - cz;
    if (dz < 0) dz = -dz;
    int32_t r = dx > dz ? dx : dz;
    if (r > max_r) max_r = r;
  }
  return max_r;
}

int mc_static_chunks_store(const uint8_t *payload, size_t len) {
  if (!payload || len == 0) return -1;
  lc_map_chunk mc;
  memset(&mc, 0, sizeof mc);
  if (lc_parse_map_chunk(payload, len, &mc) != LC_OK) return -1;
  int32_t x = mc.x;
  int32_t z = mc.z;
  lc_map_chunk_free(&mc);

  int idx = upstream_find_index(x, z);
  if (idx < 0) {
    if (g_upstream_entry_count >= MC_STATIC_CHUNKS_MAX) return -1;
    idx = g_upstream_entry_count++;
    g_upstream_entries[idx].x = x;
    g_upstream_entries[idx].z = z;
    g_upstream_entries[idx].data = NULL;
    g_upstream_entries[idx].len = 0;
  } else {
    free(g_upstream_entries[idx].data);
    g_upstream_entries[idx].data = NULL;
    g_upstream_entries[idx].len = 0;
  }

  g_upstream_entries[idx].data = (uint8_t *)malloc(len);
  if (!g_upstream_entries[idx].data) return -1;
  memcpy(g_upstream_entries[idx].data, payload, len);
  g_upstream_entries[idx].len = len;
  return 0;
}

int mc_static_chunks_lookup(int32_t x, int32_t z, const uint8_t **payload, size_t *len) {
  int idx = upstream_find_index(x, z);
  if (idx < 0) return -1;
  if (payload) *payload = g_upstream_entries[idx].data;
  if (len) *len = g_upstream_entries[idx].len;
  return 0;
}

int mc_static_chunks_send_at(int fd, int32_t x, int32_t z) {
  const uint8_t *payload = NULL;
  size_t plen = 0;
  if (mc_static_chunks_lookup(x, z, &payload, &plen) != 0) return -1;
  return mc_send_frame(fd, MC_PKT_PLAY_MAP_CHUNK, payload, plen);
}

static int upstream_send_chunk_batch_start(int fd) {
  return mc_send_frame(fd, MC_PKT_PLAY_CHUNK_BATCH_START, NULL, 0);
}

static int upstream_send_chunk_batch_finished(int fd, int32_t batch_size) {
  mc_buf b;
  memset(&b, 0, sizeof b);
  if (mc_buf_varint(&b, batch_size) != LC_OK) return -1;
  int rc = mc_send_frame(fd, MC_PKT_PLAY_CHUNK_BATCH_FINISHED, b.data, b.len);
  mc_buf_free(&b);
  return rc;
}

int mc_static_chunks_send_grid(int fd, int32_t cx, int32_t cz, int32_t radius) {
  if (radius < 0) return -1;

  int sent = 0;
  size_t wire_total = 0;

  MC_LOGI("static_server", "upstream chunks near (%d,%d) radius=%d (%zu cached total)", cx, cz, radius,
          mc_static_chunks_count());

  if (g_upstream_entry_count == 0) {
    MC_LOGW("static_server", "upstream chunks: cache empty, skipping send");
    return 0;
  }

  for (int i = 0; i < g_upstream_entry_count; i++) {
    int32_t x = g_upstream_entries[i].x;
    int32_t z = g_upstream_entries[i].z;
    if (x < cx - radius || x > cx + radius || z < cz - radius || z > cz + radius) continue;
    sent++;
  }

  if (sent == 0) {
    MC_LOGW("static_server", "upstream chunks: none within radius %d of (%d,%d)", radius, cx, cz);
    return 0;
  }

  sent = 0;
  if (upstream_send_chunk_batch_start(fd) != 0) return -1;

  for (int i = 0; i < g_upstream_entry_count; i++) {
    int32_t x = g_upstream_entries[i].x;
    int32_t z = g_upstream_entries[i].z;
    if (x < cx - radius || x > cx + radius || z < cz - radius || z > cz + radius) continue;
    if (mc_send_frame(fd, MC_PKT_PLAY_MAP_CHUNK, g_upstream_entries[i].data, g_upstream_entries[i].len) != 0)
      return -1;
    wire_total += g_upstream_entries[i].len;
    sent++;
  }

  MC_LOGI("static_server", "upstream chunks done: sent %d, wire=%zuKiB", sent, (wire_total + 512) / 1024);
  if (upstream_send_chunk_batch_finished(fd, sent) != 0) return -1;
  return 0;
}
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

void mc_chunk_stream_mark_cached_grid(mc_chunk_stream *cs, int32_t center_cx, int32_t center_cz) {
  cs->view_cx = center_cx;
  cs->view_cz = center_cz;
  cs->loaded_count = 0;
  for (int dx = -cs->radius; dx <= cs->radius; dx++) {
    for (int dz = -cs->radius; dz <= cs->radius; dz++) {
      int32_t x = center_cx + dx;
      int32_t z = center_cz + dz;
      if (mc_static_chunks_lookup(x, z, NULL, NULL) == 0) (void)chunk_mark_loaded(cs, x, z);
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
      int32_t x = to_load_x[i];
      int32_t z = to_load_z[i];
      int rc;
      if (mc_static_chunks_upstream()) {
        rc = mc_static_chunks_send_at(fd, x, z);
        if (rc != 0) {
          MC_LOGW("static_server", "upstream chunk missing on move (%d,%d)", x, z);
          continue;
        }
      } else {
        rc = mc_template_send_map_chunk_at(fd, x, z);
        if (rc != 0) return -1;
      }
      if (chunk_mark_loaded(cs, x, z) != 0) return -1;
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

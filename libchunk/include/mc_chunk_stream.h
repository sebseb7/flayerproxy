#ifndef MC_CHUNK_STREAM_H
#define MC_CHUNK_STREAM_H

#include <stddef.h>
#include <stdint.h>

#define MC_CHUNK_STREAM_MAX 128

typedef struct mc_chunk_key {
  int32_t x;
  int32_t z;
} mc_chunk_key;

typedef struct mc_chunk_stream {
  int32_t view_cx;
  int32_t view_cz;
  double pos_x;
  double pos_y;
  double pos_z;
  int has_pos;
  int32_t radius;
  mc_chunk_key loaded[MC_CHUNK_STREAM_MAX];
  int loaded_count;
} mc_chunk_stream;

void mc_chunk_stream_init(mc_chunk_stream *cs, int32_t radius);
void mc_chunk_stream_mark_grid(mc_chunk_stream *cs, int32_t center_cx, int32_t center_cz);
/** Mark only upstream-cached chunks in the view grid (registry-from mode). */
void mc_chunk_stream_mark_cached_grid(mc_chunk_stream *cs, int32_t center_cx, int32_t center_cz);

/** Update player position; syncs chunks when the view chunk changes. Returns -1 on wire error. */
int mc_chunk_stream_on_move(mc_chunk_stream *cs, int fd, double x, double y, double z);

/** True if chunk (cx,cz) is currently loaded for this client. */
int mc_chunk_stream_has_chunk(const mc_chunk_stream *cs, int32_t cx, int32_t cz);

/** Upstream map_chunk cache (registry-from mode). */
void mc_static_chunks_set_upstream(int enabled);
int mc_static_chunks_upstream(void);
void mc_static_chunks_clear(void);
size_t mc_static_chunks_count(void);
int mc_static_chunks_store(const uint8_t *payload, size_t len);
int mc_static_chunks_lookup(int32_t x, int32_t z, const uint8_t **payload, size_t *len);
int mc_static_chunks_send_at(int fd, int32_t x, int32_t z);
int32_t mc_static_chunk_radius_from_view(int32_t view_distance);
int32_t mc_static_chunks_max_cached_radius(int32_t cx, int32_t cz);
/** Chunks in cache within Chebyshev radius of (cx,cz). */
int mc_static_chunks_count_in_grid(int32_t cx, int32_t cz, int32_t radius);
/** (2*radius+1)^2 slots for a square view grid. */
int mc_static_chunks_expected_grid_count(int32_t radius);
int mc_static_chunks_send_grid(int fd, int32_t cx, int32_t cz, int32_t radius);

#endif

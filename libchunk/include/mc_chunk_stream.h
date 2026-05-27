#ifndef MC_CHUNK_STREAM_H
#define MC_CHUNK_STREAM_H

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

/** Update player position; syncs chunks when the view chunk changes. Returns -1 on wire error. */
int mc_chunk_stream_on_move(mc_chunk_stream *cs, int fd, double x, double y, double z);

/** True if chunk (cx,cz) is currently loaded for this client. */
int mc_chunk_stream_has_chunk(const mc_chunk_stream *cs, int32_t cx, int32_t cz);

#endif

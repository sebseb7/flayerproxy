#define _POSIX_C_SOURCE 200809L

/**
 * TCP receiver for sniffer chunkStream.
 * Frame: uint32 BE inner_len, uint16 BE name_len, packet name UTF-8, wire (id + payload).
 *
 * Usage:
 *   chunk_stream_receiver [-v] <port> <png_dir> [raw_dir]
 *   chunk_stream_receiver [-v] <bind_host> <port> <png_dir> [raw_dir]
 *
 *   -v, --verbose  log each processed packet (default: periodic stats only)
 */
#include "libchunk.h"

#include <arpa/inet.h>
#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define WIRE_MAX (16 * 1024 * 1024)
#define SOCK_READ_SZ (256 * 1024)
#define STATS_INTERVAL_SEC 60.0
#define PATH_MAX_LC 4096
#define PKT_NAME_MAX 64
#define POS_ARG_MAX 4

static int verbose_packets = 0;

typedef struct rx_buf {
  uint8_t *data;
  size_t len;
  size_t cap;
} rx_buf;

typedef struct perf_stats {
  uint64_t process_ns_total;
  uint64_t process_ns_window;
  size_t packets_total;
  size_t packets_window;
  struct timespec session_start;
  struct timespec window_start;
  struct timespec last_report;
} perf_stats;

static uint64_t timespec_ns(const struct timespec *t) {
  return (uint64_t)t->tv_sec * 1000000000ull + (uint64_t)t->tv_nsec;
}

static double timespec_sec_since(const struct timespec *from, const struct timespec *to) {
  return (double)(to->tv_sec - from->tv_sec) + (double)(to->tv_nsec - from->tv_nsec) / 1e9;
}

static void perf_init(perf_stats *ps) {
  memset(ps, 0, sizeof *ps);
  clock_gettime(CLOCK_MONOTONIC, &ps->session_start);
  ps->window_start = ps->session_start;
  ps->last_report = ps->session_start;
}

static void perf_add_packet(perf_stats *ps, uint64_t process_ns) {
  ps->process_ns_total += process_ns;
  ps->process_ns_window += process_ns;
  ps->packets_total++;
  ps->packets_window++;
}

static void perf_report_window(perf_stats *ps, const struct timespec *now) {
  if (ps->packets_window == 0) {
    ps->window_start = *now;
    ps->last_report = *now;
    return;
  }
  double wall = timespec_sec_since(&ps->window_start, now);
  if (wall <= 0.0) wall = STATS_INTERVAL_SEC;
  double pps = (double)ps->packets_window / wall;
  double pct = 100.0 * ((double)ps->process_ns_window / 1e9) / wall;
  fprintf(stderr,
          "stats: %.2f packets/s, handler %.1f%% occupied (last %.0fs, %zu packet(s))\n", pps,
          pct, wall, ps->packets_window);
  ps->process_ns_window = 0;
  ps->packets_window = 0;
  ps->window_start = *now;
  ps->last_report = *now;
}

static void perf_maybe_report(perf_stats *ps) {
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  if (timespec_sec_since(&ps->last_report, &now) < STATS_INTERVAL_SEC) return;
  perf_report_window(ps, &now);
}

static void perf_session_summary(const perf_stats *ps) {
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  double wall = timespec_sec_since(&ps->session_start, &now);
  if (ps->packets_total == 0) return;
  double pct = wall > 0.0 ? 100.0 * ((double)ps->process_ns_total / 1e9) / wall : 0.0;
  fprintf(stderr,
          "session: %zu packet(s), handler %.3fs total (%.1f%% of %.1fs wall)\n",
          ps->packets_total, (double)ps->process_ns_total / 1e9, pct, wall);
}

static void rx_buf_free(rx_buf *rb) {
  free(rb->data);
  rb->data = NULL;
  rb->len = rb->cap = 0;
}

static int rx_buf_reserve(rx_buf *rb, size_t need) {
  if (need <= rb->cap) return 0;
  size_t ncap = rb->cap ? rb->cap : 65536;
  while (ncap < need) {
    if (ncap > WIRE_MAX + 8) return -1;
    ncap *= 2;
  }
  uint8_t *p = (uint8_t *)realloc(rb->data, ncap);
  if (!p) return -1;
  rb->data = p;
  rb->cap = ncap;
  return 0;
}

static int rx_buf_append(rx_buf *rb, const void *src, size_t n) {
  if (rb->len + n > WIRE_MAX + 4 + PKT_NAME_MAX + 2) return -1;
  if (rx_buf_reserve(rb, rb->len + n) != 0) return -1;
  memcpy(rb->data + rb->len, src, n);
  rb->len += n;
  return 0;
}

static void rx_buf_consume(rx_buf *rb, size_t n) {
  if (n >= rb->len) {
    rb->len = 0;
    return;
  }
  memmove(rb->data, rb->data + n, rb->len - n);
  rb->len -= n;
}

/** @return 1 frame taken, 0 need more, -1 error */
static int rx_take_frame(rx_buf *rb, char *pkt_name, size_t pkt_name_len, uint8_t **wire,
                         size_t *wire_len) {
  if (rb->len < 4) return 0;
  uint32_t inner = ((uint32_t)rb->data[0] << 24) | ((uint32_t)rb->data[1] << 16) |
                   ((uint32_t)rb->data[2] << 8) | (uint32_t)rb->data[3];
  if (inner < 2 || inner > WIRE_MAX + PKT_NAME_MAX) return -1;
  if (rb->len < 4 + (size_t)inner) return 0;

  uint16_t nlen = ((uint16_t)rb->data[4] << 8) | (uint16_t)rb->data[5];
  if (nlen == 0 || nlen >= pkt_name_len || (size_t)(2 + nlen) > inner) return -1;

  memcpy(pkt_name, rb->data + 6, nlen);
  pkt_name[nlen] = '\0';

  size_t wlen = inner - 2 - (size_t)nlen;
  const uint8_t *wsrc = rb->data + 6 + nlen;
  uint8_t *wcopy = (uint8_t *)malloc(wlen);
  if (!wcopy) return -1;
  memcpy(wcopy, wsrc, wlen);
  rx_buf_consume(rb, 4 + (size_t)inner);
  *wire = wcopy;
  *wire_len = wlen;
  return 1;
}

static int mkdir_p(const char *dir) {
  char tmp[PATH_MAX_LC];
  size_t len = strlen(dir);
  if (len == 0 || len >= sizeof tmp) return -1;
  snprintf(tmp, sizeof tmp, "%s", dir);
  if (tmp[len - 1] == '/') tmp[len - 1] = '\0';
  for (char *p = tmp + 1; *p; p++) {
    if (*p != '/') continue;
    *p = '\0';
    if (mkdir(tmp, 0755) != 0 && errno != EEXIST) return -1;
    *p = '/';
  }
  if (mkdir(tmp, 0755) != 0 && errno != EEXIST) return -1;
  return 0;
}

static int path_fits(int n, size_t outlen) { return n > 0 && (size_t)n < outlen; }

/** Three-level entity id dirs: eu<hi>/eu<mid>/e<id> (unsigned hi/mid shards). */
static int append_entity_id_dirs(char *out, size_t outlen, size_t *pos, int32_t entity_id) {
  uint32_t u = (uint32_t)entity_id;
  int n = snprintf(out + *pos, outlen - *pos, "/eu%u/eu%u/e%d", (unsigned)(u >> 16),
                   (unsigned)((u >> 8) & 0xffu), entity_id);
  if (!path_fits(n, outlen - *pos)) return -1;
  *pos += (size_t)n;
  return 0;
}

static void png_path_for_chunk(const char *png_dir, int32_t cx, int32_t cz, char *out, size_t outlen) {
  int32_t rx = cx >> 5;
  int32_t rz = cz >> 5;
  int32_t wx = cx * 16;
  int32_t wz = cz * 16;
  snprintf(out, outlen, "%s/rx%d/rz%d/cx%d/cz%d/x%d_z%d.png", png_dir, rx, rz, cx, cz, wx, wz);
}

static int raw_path_map_chunk(char *out, size_t outlen, const char *raw_dir, int32_t cx, int32_t cz) {
  int32_t rx = cx >> 5;
  int32_t rz = cz >> 5;
  int32_t wx = cx * 16;
  int32_t wz = cz * 16;
  int n = snprintf(out, outlen, "%s/map_chunk/rx%d/rz%d/cx%d/cz%d/x%d_z%d.map_chunk.wire", raw_dir,
                   rx, rz, cx, cz, wx, wz);
  return path_fits(n, outlen) ? 0 : -1;
}

static int raw_path_entity(char *out, size_t outlen, const char *raw_dir, const char *pkt,
                           int32_t entity_id) {
  size_t pos = 0;
  int n = snprintf(out, outlen, "%s/%s", raw_dir, pkt);
  if (!path_fits(n, outlen)) return -1;
  pos = (size_t)n;
  if (append_entity_id_dirs(out, outlen, &pos, entity_id) != 0) return -1;
  n = snprintf(out + pos, outlen - pos, "/%d.%s.wire", entity_id, pkt);
  return path_fits(n, outlen - pos) ? 0 : -1;
}

static int raw_path_spawn_wire(char *out, size_t outlen, const char *raw_dir, int32_t cx, int32_t cz,
                               int32_t wx, int32_t wy, int32_t wz) {
  int32_t rx = cx >> 5;
  int32_t rz = cz >> 5;
  int n = snprintf(out, outlen,
                   "%s/spawn_entity/rx%d/rz%d/cx%d/cz%d/x%d_y%d_z%d.spawn_entity.wire", raw_dir, rx,
                   rz, cx, cz, wx, wy, wz);
  return path_fits(n, outlen) ? 0 : -1;
}

static int raw_path_tile_entity(char *out, size_t outlen, const char *raw_dir, int32_t cx, int32_t cz,
                                int32_t wx, int32_t wy, int32_t wz) {
  int32_t rx = cx >> 5;
  int32_t rz = cz >> 5;
  int n = snprintf(out, outlen,
                   "%s/tile_entity_data/rx%d/rz%d/cx%d/cz%d/x%d_y%d_z%d.tile_entity_data.wire",
                   raw_dir, rx, rz, cx, cz, wx, wy, wz);
  return path_fits(n, outlen) ? 0 : -1;
}

static int raw_path_chunk_wire(char *out, size_t outlen, const char *raw_dir, const char *pkt,
                               int32_t cx, int32_t cz) {
  int32_t rx = cx >> 5;
  int32_t rz = cz >> 5;
  int32_t wx = cx * 16;
  int32_t wz = cz * 16;
  int n = snprintf(out, outlen, "%s/%s/rx%d/rz%d/cx%d/cz%d/x%d_z%d.%s.wire", raw_dir, pkt, rx, rz,
                   cx, cz, wx, wz, pkt);
  return path_fits(n, outlen) ? 0 : -1;
}

static int raw_path_block_wire(char *out, size_t outlen, const char *raw_dir, int32_t cx, int32_t cz,
                               int32_t wx, int32_t wy, int32_t wz) {
  int32_t rx = cx >> 5;
  int32_t rz = cz >> 5;
  int n = snprintf(out, outlen,
                   "%s/block_change/rx%d/rz%d/cx%d/cz%d/x%d_y%d_z%d.block_change.wire", raw_dir, rx,
                   rz, cx, cz, wx, wy, wz);
  return path_fits(n, outlen) ? 0 : -1;
}

/** Packets without a coordinate parser yet (e.g. entity_teleport). */
static int raw_path_pkt_flat(char *out, size_t outlen, const char *raw_dir, const char *pkt) {
  int n = snprintf(out, outlen, "%s/%s/%s.%s.wire", raw_dir, pkt, pkt, pkt);
  return path_fits(n, outlen) ? 0 : -1;
}

/** player/, config/, or misc/ archives (latest packet overwrites). */
static int raw_path_category_flat(char *out, size_t outlen, const char *raw_dir, const char *category,
                                  const char *pkt) {
  int n = snprintf(out, outlen, "%s/%s/%s/%s.%s.wire", raw_dir, category, pkt, pkt, pkt);
  return path_fits(n, outlen) ? 0 : -1;
}

static int raw_path_player_position(char *out, size_t outlen, const char *raw_dir, int32_t cx, int32_t cz,
                                    double wx, double wy, double wz) {
  int32_t rx = cx >> 5;
  int32_t rz = cz >> 5;
  int n = snprintf(out, outlen,
                   "%s/player/position/rx%d/rz%d/cx%d/cz%d/x%d_y%d_z%d.position.wire", raw_dir, rx, rz,
                   cx, cz, (int)floor(wx), (int)floor(wy), (int)floor(wz));
  return path_fits(n, outlen) ? 0 : -1;
}

typedef struct {
  const char *pkt;
  const char *category;
} stream_pkt_category_t;

/** Mirrors sniffer chunkStream STREAM_* allowlists (except block/entity coord packets). */
static const stream_pkt_category_t STREAM_PKT_CATEGORIES[] = {
    /* player */
    {"login", "player"},
    {"update_health", "player"},
    {"experience", "player"},
    {"abilities", "player"},
    {"entity_status", "player"},
    {"spawn_position", "player"},
    {"difficulty", "player"},
    {"respawn", "player"},
    {"game_state_change", "player"},
    {"window_items", "player"},
    {"set_slot", "player"},
    {"held_item_slot", "player"},
    {"set_player_inventory", "player"},
    {"set_cursor_item", "player"},
    /* config */
    {"tags", "config"},
    {"server_data", "config"},
    {"declare_commands", "config"},
    {"declare_recipes", "config"},
    {"update_recipes", "config"},
    {"advancements", "config"},
    {"recipe_book_add", "config"},
    {"recipe_book_settings", "config"},
    {"chunk_batch_start", "config"},
    {"chunk_batch_finished", "config"},
    /* misc */
    {"update_time", "misc"},
    {"initialize_world_border", "misc"},
    {"world_border_center", "misc"},
    {"world_border_size", "misc"},
    {"world_border_lerp_size", "misc"},
    {"world_border_warning_delay", "misc"},
    {"world_border_warning_reach", "misc"},
    {"simulation_distance", "misc"},
    {"update_view_distance", "misc"},
    {"update_view_position", "misc"},
    {"player_info", "misc"},
    {"player_remove", "misc"},
    {"playerlist_header", "misc"},
    {"scoreboard_objective", "misc"},
    {"scoreboard_display_objective", "misc"},
    {"scoreboard_score", "misc"},
    {"reset_score", "misc"},
    {"teams", "misc"},
    {"boss_bar", "misc"},
    {"tracked_waypoint", "misc"},
    {NULL, NULL},
};

static const char *stream_pkt_category(const char *pkt) {
  for (const stream_pkt_category_t *e = STREAM_PKT_CATEGORIES; e->pkt; e++) {
    if (strcmp(pkt, e->pkt) == 0) return e->category;
  }
  return NULL;
}

static int ensure_parent_dir(const char *path) {
  char dir[PATH_MAX_LC];
  snprintf(dir, sizeof dir, "%s", path);
  char *slash = strrchr(dir, '/');
  if (!slash) return -1;
  *slash = '\0';
  return mkdir_p(dir);
}

static int write_raw_wire(const char *path, const uint8_t *wire, size_t wire_len) {
  if (ensure_parent_dir(path) != 0) return -1;

  FILE *f = fopen(path, "wb");
  if (!f) return -1;
  int ok = fwrite(wire, 1, wire_len, f) == wire_len && fclose(f) == 0;
  if (!ok && f) fclose(f);
  return ok ? 0 : -1;
}

static int skip_payload(const uint8_t *wire, size_t wire_len, const uint8_t **payload,
                        size_t *payload_len) {
  return lc_skip_packet_id(wire, wire_len, payload, payload_len) == LC_OK ? 0 : -1;
}

static int archive_raw(const char *raw_dir, const char *path, const uint8_t *wire, size_t wire_len) {
  if (!raw_dir) return 0;
  return write_raw_wire(path, wire, wire_len) == 0 ? 0 : -1;
}

static void log_done(const char *pkt, const char *detail, uint64_t handler_ns) {
  if (!verbose_packets) return;
  fprintf(stderr, "%s %s %.3fms\n", pkt, detail, (double)handler_ns / 1e6);
}

static int process_map_chunk(const char *png_dir, const char *raw_dir, const uint8_t *wire,
                             size_t wire_len, uint64_t *handler_ns) {
  struct timespec t0, t1;
  clock_gettime(CLOCK_MONOTONIC, &t0);

  const uint8_t *payload = wire;
  size_t payload_len = wire_len;
  if (skip_payload(wire, wire_len, &payload, &payload_len) != 0) {
    fprintf(stderr, "skip: map_chunk bad packet id (%zu bytes)\n", wire_len);
    return 1;
  }

  lc_map_chunk mc;
  memset(&mc, 0, sizeof mc);
  if (lc_parse_map_chunk(payload, payload_len, &mc) != LC_OK) {
    fprintf(stderr, "skip: map_chunk parse error (%zu bytes)\n", wire_len);
    return 1;
  }

  char path[PATH_MAX_LC];
  if (raw_dir && raw_path_map_chunk(path, sizeof path, raw_dir, mc.x, mc.z) == 0) {
    if (write_raw_wire(path, wire, wire_len) != 0) {
      fprintf(stderr, "raw archive error: %s\n", path);
      lc_map_chunk_free(&mc);
      return -1;
    }
  }

  png_path_for_chunk(png_dir, mc.x, mc.z, path, sizeof path);
  if (ensure_parent_dir(path) != 0) {
    fprintf(stderr, "png mkdir error: %s (%s)\n", path, strerror(errno));
    lc_map_chunk_free(&mc);
    return 1;
  }
  if (lc_map_chunk_write_top_png(&mc, path) != LC_OK) {
    fprintf(stderr, "png error: %s\n", path);
    lc_map_chunk_free(&mc);
    return 1;
  }

  int32_t cx = mc.x;
  int32_t cz = mc.z;
  lc_map_chunk_free(&mc);

  clock_gettime(CLOCK_MONOTONIC, &t1);
  *handler_ns = timespec_ns(&t1) - timespec_ns(&t0);
  if (verbose_packets)
    fprintf(stderr, "png %s (chunk %d,%d) %.3fms\n", path, cx, cz, (double)*handler_ns / 1e6);
  return 0;
}

static int process_entity_equipment(const char *raw_dir, const uint8_t *wire, size_t wire_len,
                                    uint64_t *handler_ns) {
  struct timespec t0, t1;
  clock_gettime(CLOCK_MONOTONIC, &t0);

  const uint8_t *payload = wire;
  size_t payload_len = wire_len;
  if (skip_payload(wire, wire_len, &payload, &payload_len) != 0) return 1;

  lc_entity_equipment ee;
  memset(&ee, 0, sizeof ee);
  if (lc_parse_entity_equipment(payload, payload_len, &ee) != LC_OK) return 1;

  char path[PATH_MAX_LC];
  char detail[64];
  snprintf(detail, sizeof detail, "e%d", ee.entity_id);
  if (raw_dir && raw_path_entity(path, sizeof path, raw_dir, "entity_equipment", ee.entity_id) ==
                     0) {
    if (write_raw_wire(path, wire, wire_len) != 0) {
      lc_entity_equipment_free(&ee);
      return -1;
    }
  }
  lc_entity_equipment_free(&ee);

  clock_gettime(CLOCK_MONOTONIC, &t1);
  *handler_ns = timespec_ns(&t1) - timespec_ns(&t0);
  log_done("entity_equipment", detail, *handler_ns);
  return 0;
}

static int process_entity_update_attributes(const char *raw_dir, const uint8_t *wire, size_t wire_len,
                                            uint64_t *handler_ns) {
  struct timespec t0, t1;
  clock_gettime(CLOCK_MONOTONIC, &t0);

  const uint8_t *payload = wire;
  size_t payload_len = wire_len;
  if (skip_payload(wire, wire_len, &payload, &payload_len) != 0) return 1;

  lc_entity_update_attributes eua;
  memset(&eua, 0, sizeof eua);
  if (lc_parse_entity_update_attributes(payload, payload_len, &eua) != LC_OK) return 1;

  char path[PATH_MAX_LC];
  char detail[64];
  snprintf(detail, sizeof detail, "e%d", eua.entity_id);
  if (raw_dir &&
      raw_path_entity(path, sizeof path, raw_dir, "entity_update_attributes", eua.entity_id) == 0) {
    if (write_raw_wire(path, wire, wire_len) != 0) {
      lc_entity_update_attributes_free(&eua);
      return -1;
    }
  }
  lc_entity_update_attributes_free(&eua);

  clock_gettime(CLOCK_MONOTONIC, &t1);
  *handler_ns = timespec_ns(&t1) - timespec_ns(&t0);
  log_done("entity_update_attributes", detail, *handler_ns);
  return 0;
}

static int process_set_passengers(const char *raw_dir, const uint8_t *wire, size_t wire_len,
                                  uint64_t *handler_ns) {
  struct timespec t0, t1;
  clock_gettime(CLOCK_MONOTONIC, &t0);

  const uint8_t *payload = wire;
  size_t payload_len = wire_len;
  if (skip_payload(wire, wire_len, &payload, &payload_len) != 0) return 1;

  lc_set_passengers sp;
  memset(&sp, 0, sizeof sp);
  if (lc_parse_set_passengers(payload, payload_len, &sp) != LC_OK) return 1;

  char path[PATH_MAX_LC];
  char detail[64];
  snprintf(detail, sizeof detail, "e%d passengers=%zu", sp.entity_id, sp.passenger_count);
  if (raw_dir && raw_path_entity(path, sizeof path, raw_dir, "set_passengers", sp.entity_id) == 0) {
    if (write_raw_wire(path, wire, wire_len) != 0) {
      lc_set_passengers_free(&sp);
      return -1;
    }
  }
  lc_set_passengers_free(&sp);

  clock_gettime(CLOCK_MONOTONIC, &t1);
  *handler_ns = timespec_ns(&t1) - timespec_ns(&t0);
  log_done("set_passengers", detail, *handler_ns);
  return 0;
}

static int process_spawn_entity(const char *raw_dir, const uint8_t *wire, size_t wire_len,
                                uint64_t *handler_ns) {
  struct timespec t0, t1;
  clock_gettime(CLOCK_MONOTONIC, &t0);

  const uint8_t *payload = wire;
  size_t payload_len = wire_len;
  if (skip_payload(wire, wire_len, &payload, &payload_len) != 0) return 1;

  lc_spawn_entity se;
  if (lc_parse_spawn_entity(payload, payload_len, &se) != LC_OK) return 1;

  int32_t wx = (int32_t)floor(se.x);
  int32_t wy = (int32_t)floor(se.y);
  int32_t wz = (int32_t)floor(se.z);
  int32_t cx = wx >> 4;
  int32_t cz = wz >> 4;

  char path[PATH_MAX_LC];
  char detail[96];
  snprintf(detail, sizeof detail, "e%d type=%d (%d,%d,%d)", se.entity_id, se.type, wx, wy, wz);
  if (raw_dir && raw_path_spawn_wire(path, sizeof path, raw_dir, cx, cz, wx, wy, wz) == 0) {
    if (write_raw_wire(path, wire, wire_len) != 0) return -1;
  }

  clock_gettime(CLOCK_MONOTONIC, &t1);
  *handler_ns = timespec_ns(&t1) - timespec_ns(&t0);
  log_done("spawn_entity", detail, *handler_ns);
  return 0;
}

static int process_tile_entity_data(const char *raw_dir, const uint8_t *wire, size_t wire_len,
                                    uint64_t *handler_ns) {
  struct timespec t0, t1;
  clock_gettime(CLOCK_MONOTONIC, &t0);

  const uint8_t *payload = wire;
  size_t payload_len = wire_len;
  if (skip_payload(wire, wire_len, &payload, &payload_len) != 0) return 1;

  lc_tile_entity_data ted;
  memset(&ted, 0, sizeof ted);
  if (lc_parse_tile_entity_data(payload, payload_len, &ted) != LC_OK) return 1;

  int32_t cx = ted.location.x >> 4;
  int32_t cz = ted.location.z >> 4;

  char path[PATH_MAX_LC];
  char detail[96];
  snprintf(detail, sizeof detail, "(%d,%d,%d) action=%d", ted.location.x, ted.location.y,
           ted.location.z, ted.action);
  if (raw_dir &&
      raw_path_tile_entity(path, sizeof path, raw_dir, cx, cz, ted.location.x, ted.location.y,
                           ted.location.z) == 0) {
    if (write_raw_wire(path, wire, wire_len) != 0) {
      lc_tile_entity_data_free(&ted);
      return -1;
    }
  }
  lc_tile_entity_data_free(&ted);

  clock_gettime(CLOCK_MONOTONIC, &t1);
  *handler_ns = timespec_ns(&t1) - timespec_ns(&t0);
  log_done("tile_entity_data", detail, *handler_ns);
  return 0;
}

static int process_block_change(const char *raw_dir, const uint8_t *wire, size_t wire_len,
                                uint64_t *handler_ns) {
  struct timespec t0, t1;
  clock_gettime(CLOCK_MONOTONIC, &t0);

  const uint8_t *payload = wire;
  size_t payload_len = wire_len;
  if (skip_payload(wire, wire_len, &payload, &payload_len) != 0) return 1;

  lc_block_change bc;
  if (lc_parse_block_change(payload, payload_len, &bc) != LC_OK) return 1;

  int32_t cx = bc.location.x >> 4;
  int32_t cz = bc.location.z >> 4;
  char path[PATH_MAX_LC];
  char detail[64];
  snprintf(detail, sizeof detail, "(%d,%d,%d) type=%d", bc.location.x, bc.location.y, bc.location.z,
           bc.type);
  if (raw_path_block_wire(path, sizeof path, raw_dir, cx, cz, bc.location.x, bc.location.y,
                          bc.location.z) == 0) {
    if (archive_raw(raw_dir, path, wire, wire_len) != 0) return -1;
  }

  clock_gettime(CLOCK_MONOTONIC, &t1);
  *handler_ns = timespec_ns(&t1) - timespec_ns(&t0);
  log_done("block_change", detail, *handler_ns);
  return 0;
}

static int process_multi_block_change(const char *raw_dir, const uint8_t *wire, size_t wire_len,
                                      uint64_t *handler_ns) {
  struct timespec t0, t1;
  clock_gettime(CLOCK_MONOTONIC, &t0);

  const uint8_t *payload = wire;
  size_t payload_len = wire_len;
  if (skip_payload(wire, wire_len, &payload, &payload_len) != 0) return 1;

  lc_multi_block_change mbc;
  memset(&mbc, 0, sizeof mbc);
  if (lc_parse_multi_block_change(payload, payload_len, &mbc) != LC_OK) return 1;

  char path[PATH_MAX_LC];
  char detail[64];
  snprintf(detail, sizeof detail, "chunk(%d,%d) records=%zu", mbc.chunk_coordinates.x,
           mbc.chunk_coordinates.z, mbc.record_count);
  if (raw_path_chunk_wire(path, sizeof path, raw_dir, "multi_block_change", mbc.chunk_coordinates.x,
                          mbc.chunk_coordinates.z) == 0) {
    if (archive_raw(raw_dir, path, wire, wire_len) != 0) {
      lc_multi_block_change_free(&mbc);
      return -1;
    }
  }
  lc_multi_block_change_free(&mbc);

  clock_gettime(CLOCK_MONOTONIC, &t1);
  *handler_ns = timespec_ns(&t1) - timespec_ns(&t0);
  log_done("multi_block_change", detail, *handler_ns);
  return 0;
}

static int process_update_light(const char *raw_dir, const uint8_t *wire, size_t wire_len,
                                uint64_t *handler_ns) {
  struct timespec t0, t1;
  clock_gettime(CLOCK_MONOTONIC, &t0);

  const uint8_t *payload = wire;
  size_t payload_len = wire_len;
  if (skip_payload(wire, wire_len, &payload, &payload_len) != 0) return 1;

  lc_update_light ul;
  memset(&ul, 0, sizeof ul);
  if (lc_parse_update_light(payload, payload_len, &ul) != LC_OK) return 1;

  char path[PATH_MAX_LC];
  char detail[48];
  snprintf(detail, sizeof detail, "chunk(%d,%d)", ul.chunk_x, ul.chunk_z);
  if (raw_path_chunk_wire(path, sizeof path, raw_dir, "update_light", ul.chunk_x, ul.chunk_z) == 0) {
    if (archive_raw(raw_dir, path, wire, wire_len) != 0) {
      lc_update_light_free(&ul);
      return -1;
    }
  }
  lc_update_light_free(&ul);

  clock_gettime(CLOCK_MONOTONIC, &t1);
  *handler_ns = timespec_ns(&t1) - timespec_ns(&t0);
  log_done("update_light", detail, *handler_ns);
  return 0;
}

static int process_entity_metadata(const char *raw_dir, const uint8_t *wire, size_t wire_len,
                                   uint64_t *handler_ns) {
  struct timespec t0, t1;
  clock_gettime(CLOCK_MONOTONIC, &t0);

  const uint8_t *payload = wire;
  size_t payload_len = wire_len;
  if (skip_payload(wire, wire_len, &payload, &payload_len) != 0) return 1;

  lc_entity_metadata em;
  memset(&em, 0, sizeof em);
  if (lc_parse_entity_metadata(payload, payload_len, &em) != LC_OK) return 1;

  char path[PATH_MAX_LC];
  char detail[64];
  snprintf(detail, sizeof detail, "e%d entries=%zu", em.entity_id, em.metadata.count);
  if (raw_path_entity(path, sizeof path, raw_dir, "entity_metadata", em.entity_id) == 0) {
    if (archive_raw(raw_dir, path, wire, wire_len) != 0) {
      lc_entity_metadata_free(&em);
      return -1;
    }
  }
  lc_entity_metadata_free(&em);

  clock_gettime(CLOCK_MONOTONIC, &t1);
  *handler_ns = timespec_ns(&t1) - timespec_ns(&t0);
  log_done("entity_metadata", detail, *handler_ns);
  return 0;
}

static int process_entity_destroy_pkt(const char *raw_dir, const uint8_t *wire, size_t wire_len,
                                      uint64_t *handler_ns) {
  struct timespec t0, t1;
  clock_gettime(CLOCK_MONOTONIC, &t0);

  const uint8_t *payload = wire;
  size_t payload_len = wire_len;
  if (skip_payload(wire, wire_len, &payload, &payload_len) != 0) return 1;

  lc_entity_destroy ed;
  memset(&ed, 0, sizeof ed);
  if (lc_parse_entity_destroy(payload, payload_len, &ed) != LC_OK) return 1;

  char path[PATH_MAX_LC];
  char detail[64];
  snprintf(detail, sizeof detail, "count=%zu", ed.count);
  for (size_t i = 0; i < ed.count; i++) {
    if (raw_path_entity(path, sizeof path, raw_dir, "entity_destroy", ed.entity_ids[i]) == 0) {
      if (archive_raw(raw_dir, path, wire, wire_len) != 0) {
        lc_entity_destroy_free(&ed);
        return -1;
      }
    }
  }
  lc_entity_destroy_free(&ed);

  clock_gettime(CLOCK_MONOTONIC, &t1);
  *handler_ns = timespec_ns(&t1) - timespec_ns(&t0);
  log_done("entity_destroy", detail, *handler_ns);
  return 0;
}

static int process_entity_id_only(const char *raw_dir, const char *pkt, int32_t entity_id,
                                  const uint8_t *wire, size_t wire_len, uint64_t *handler_ns) {
  char path[PATH_MAX_LC];
  char detail[64];
  snprintf(detail, sizeof detail, "e%d", entity_id);
  if (raw_path_entity(path, sizeof path, raw_dir, pkt, entity_id) == 0) {
    if (archive_raw(raw_dir, path, wire, wire_len) != 0) return -1;
  }
  log_done(pkt, detail, *handler_ns);
  return 0;
}

static int process_sync_entity_position(const char *raw_dir, const uint8_t *wire, size_t wire_len,
                                        uint64_t *handler_ns) {
  struct timespec t0, t1;
  clock_gettime(CLOCK_MONOTONIC, &t0);

  const uint8_t *payload = wire;
  size_t payload_len = wire_len;
  if (skip_payload(wire, wire_len, &payload, &payload_len) != 0) return 1;

  lc_sync_entity_position sep;
  if (lc_parse_sync_entity_position(payload, payload_len, &sep) != LC_OK) return 1;

  clock_gettime(CLOCK_MONOTONIC, &t1);
  *handler_ns = timespec_ns(&t1) - timespec_ns(&t0);
  return process_entity_id_only(raw_dir, "sync_entity_position", sep.entity_id, wire, wire_len,
                                handler_ns);
}

static int process_rel_entity_move(const char *raw_dir, const uint8_t *wire, size_t wire_len,
                                   uint64_t *handler_ns) {
  struct timespec t0, t1;
  clock_gettime(CLOCK_MONOTONIC, &t0);

  const uint8_t *payload = wire;
  size_t payload_len = wire_len;
  if (skip_payload(wire, wire_len, &payload, &payload_len) != 0) return 1;

  lc_rel_entity_move m;
  if (lc_parse_rel_entity_move(payload, payload_len, &m) != LC_OK) return 1;

  clock_gettime(CLOCK_MONOTONIC, &t1);
  *handler_ns = timespec_ns(&t1) - timespec_ns(&t0);
  return process_entity_id_only(raw_dir, "rel_entity_move", m.entity_id, wire, wire_len, handler_ns);
}

static int process_entity_move_look(const char *raw_dir, const uint8_t *wire, size_t wire_len,
                                    uint64_t *handler_ns) {
  struct timespec t0, t1;
  clock_gettime(CLOCK_MONOTONIC, &t0);

  const uint8_t *payload = wire;
  size_t payload_len = wire_len;
  if (skip_payload(wire, wire_len, &payload, &payload_len) != 0) return 1;

  lc_entity_move_look m;
  if (lc_parse_entity_move_look(payload, payload_len, &m) != LC_OK) return 1;

  clock_gettime(CLOCK_MONOTONIC, &t1);
  *handler_ns = timespec_ns(&t1) - timespec_ns(&t0);
  return process_entity_id_only(raw_dir, "entity_move_look", m.entity_id, wire, wire_len, handler_ns);
}

static int process_entity_velocity_pkt(const char *raw_dir, const uint8_t *wire, size_t wire_len,
                                       uint64_t *handler_ns) {
  struct timespec t0, t1;
  clock_gettime(CLOCK_MONOTONIC, &t0);

  const uint8_t *payload = wire;
  size_t payload_len = wire_len;
  if (skip_payload(wire, wire_len, &payload, &payload_len) != 0) return 1;

  lc_entity_velocity ev;
  if (lc_parse_entity_velocity(payload, payload_len, &ev) != LC_OK) return 1;

  clock_gettime(CLOCK_MONOTONIC, &t1);
  *handler_ns = timespec_ns(&t1) - timespec_ns(&t0);
  return process_entity_id_only(raw_dir, "entity_velocity", ev.entity_id, wire, wire_len, handler_ns);
}

static int process_entity_head_rotation(const char *raw_dir, const uint8_t *wire, size_t wire_len,
                                        uint64_t *handler_ns) {
  struct timespec t0, t1;
  clock_gettime(CLOCK_MONOTONIC, &t0);

  const uint8_t *payload = wire;
  size_t payload_len = wire_len;
  if (skip_payload(wire, wire_len, &payload, &payload_len) != 0) return 1;

  lc_entity_head_rotation hr;
  if (lc_parse_entity_head_rotation(payload, payload_len, &hr) != LC_OK) return 1;

  clock_gettime(CLOCK_MONOTONIC, &t1);
  *handler_ns = timespec_ns(&t1) - timespec_ns(&t0);
  return process_entity_id_only(raw_dir, "entity_head_rotation", hr.entity_id, wire, wire_len,
                              handler_ns);
}

static int process_raw_flat(const char *raw_dir, const char *pkt, const uint8_t *wire,
                            size_t wire_len, uint64_t *handler_ns) {
  struct timespec t0, t1;
  clock_gettime(CLOCK_MONOTONIC, &t0);

  char path[PATH_MAX_LC];
  if (raw_path_pkt_flat(path, sizeof path, raw_dir, pkt) == 0) {
    if (archive_raw(raw_dir, path, wire, wire_len) != 0) return -1;
  }

  clock_gettime(CLOCK_MONOTONIC, &t1);
  *handler_ns = timespec_ns(&t1) - timespec_ns(&t0);
  log_done(pkt, "flat", *handler_ns);
  return 0;
}

static int process_raw_category(const char *raw_dir, const char *category, const char *pkt,
                                const uint8_t *wire, size_t wire_len, uint64_t *handler_ns) {
  struct timespec t0, t1;
  clock_gettime(CLOCK_MONOTONIC, &t0);

  char path[PATH_MAX_LC];
  if (raw_path_category_flat(path, sizeof path, raw_dir, category, pkt) == 0) {
    if (archive_raw(raw_dir, path, wire, wire_len) != 0) return -1;
  }

  clock_gettime(CLOCK_MONOTONIC, &t1);
  *handler_ns = timespec_ns(&t1) - timespec_ns(&t0);
  if (verbose_packets) {
    char detail[80];
    snprintf(detail, sizeof detail, "%s/%s", category, pkt);
    log_done(pkt, detail, *handler_ns);
  }
  return 0;
}

static int process_position_pkt(const char *raw_dir, const uint8_t *wire, size_t wire_len,
                                uint64_t *handler_ns) {
  struct timespec t0, t1;
  clock_gettime(CLOCK_MONOTONIC, &t0);

  const uint8_t *payload = wire;
  size_t payload_len = wire_len;
  if (skip_payload(wire, wire_len, &payload, &payload_len) != 0) return 1;

  lc_position pos;
  memset(&pos, 0, sizeof pos);
  if (lc_parse_position(payload, payload_len, &pos) != LC_OK) return 1;

  char path[PATH_MAX_LC];
  int32_t cx = (int32_t)floor(pos.x / 16.0);
  int32_t cz = (int32_t)floor(pos.z / 16.0);
  if (raw_dir &&
      raw_path_player_position(path, sizeof path, raw_dir, cx, cz, pos.x, pos.y, pos.z) == 0) {
    if (archive_raw(raw_dir, path, wire, wire_len) != 0) return -1;
  }

  clock_gettime(CLOCK_MONOTONIC, &t1);
  *handler_ns = timespec_ns(&t1) - timespec_ns(&t0);
  log_done("position", "player/position", *handler_ns);
  return 0;
}

static int process_unload_chunk_pkt(const char *raw_dir, const uint8_t *wire, size_t wire_len,
                                    uint64_t *handler_ns) {
  struct timespec t0, t1;
  clock_gettime(CLOCK_MONOTONIC, &t0);

  const uint8_t *payload = wire;
  size_t payload_len = wire_len;
  if (skip_payload(wire, wire_len, &payload, &payload_len) != 0) return 1;

  lc_unload_chunk uc;
  memset(&uc, 0, sizeof uc);
  if (lc_parse_unload_chunk(payload, payload_len, &uc) != LC_OK) return 1;

  char path[PATH_MAX_LC];
  if (raw_dir && raw_path_chunk_wire(path, sizeof path, raw_dir, "unload_chunk", uc.x, uc.z) == 0) {
    if (archive_raw(raw_dir, path, wire, wire_len) != 0) return -1;
  }

  clock_gettime(CLOCK_MONOTONIC, &t1);
  *handler_ns = timespec_ns(&t1) - timespec_ns(&t0);
  log_done("unload_chunk", "chunk", *handler_ns);
  return 0;
}

static int process_stream_packet(const char *pkt_name, const char *png_dir, const char *raw_dir,
                                 const uint8_t *wire, size_t wire_len, perf_stats *ps) {
  uint64_t handler_ns = 0;
  int rc = 1;

  if (strcmp(pkt_name, "map_chunk") == 0) {
    rc = process_map_chunk(png_dir, raw_dir, wire, wire_len, &handler_ns);
  } else if (strcmp(pkt_name, "block_change") == 0) {
    rc = process_block_change(raw_dir, wire, wire_len, &handler_ns);
  } else if (strcmp(pkt_name, "multi_block_change") == 0) {
    rc = process_multi_block_change(raw_dir, wire, wire_len, &handler_ns);
  } else if (strcmp(pkt_name, "update_light") == 0) {
    rc = process_update_light(raw_dir, wire, wire_len, &handler_ns);
  } else if (strcmp(pkt_name, "tile_entity_data") == 0) {
    rc = process_tile_entity_data(raw_dir, wire, wire_len, &handler_ns);
  } else if (strcmp(pkt_name, "entity_equipment") == 0) {
    rc = process_entity_equipment(raw_dir, wire, wire_len, &handler_ns);
  } else if (strcmp(pkt_name, "entity_update_attributes") == 0) {
    rc = process_entity_update_attributes(raw_dir, wire, wire_len, &handler_ns);
  } else if (strcmp(pkt_name, "set_passengers") == 0) {
    rc = process_set_passengers(raw_dir, wire, wire_len, &handler_ns);
  } else if (strcmp(pkt_name, "spawn_entity") == 0) {
    rc = process_spawn_entity(raw_dir, wire, wire_len, &handler_ns);
  } else if (strcmp(pkt_name, "entity_metadata") == 0) {
    rc = process_entity_metadata(raw_dir, wire, wire_len, &handler_ns);
  } else if (strcmp(pkt_name, "entity_destroy") == 0) {
    rc = process_entity_destroy_pkt(raw_dir, wire, wire_len, &handler_ns);
  } else if (strcmp(pkt_name, "rel_entity_move") == 0) {
    rc = process_rel_entity_move(raw_dir, wire, wire_len, &handler_ns);
  } else if (strcmp(pkt_name, "entity_move_look") == 0) {
    rc = process_entity_move_look(raw_dir, wire, wire_len, &handler_ns);
  } else if (strcmp(pkt_name, "sync_entity_position") == 0) {
    rc = process_sync_entity_position(raw_dir, wire, wire_len, &handler_ns);
  } else if (strcmp(pkt_name, "entity_velocity") == 0) {
    rc = process_entity_velocity_pkt(raw_dir, wire, wire_len, &handler_ns);
  } else if (strcmp(pkt_name, "entity_head_rotation") == 0) {
    rc = process_entity_head_rotation(raw_dir, wire, wire_len, &handler_ns);
  } else if (strcmp(pkt_name, "entity_look") == 0) {
    rc = process_raw_flat(raw_dir, pkt_name, wire, wire_len, &handler_ns);
  } else if (strcmp(pkt_name, "entity_teleport") == 0 || strcmp(pkt_name, "entity_effect") == 0 ||
             strcmp(pkt_name, "remove_entity_effect") == 0) {
    rc = process_raw_flat(raw_dir, pkt_name, wire, wire_len, &handler_ns);
  } else if (strcmp(pkt_name, "position") == 0) {
    rc = process_position_pkt(raw_dir, wire, wire_len, &handler_ns);
  } else if (strcmp(pkt_name, "unload_chunk") == 0) {
    rc = process_unload_chunk_pkt(raw_dir, wire, wire_len, &handler_ns);
  } else {
    const char *category = stream_pkt_category(pkt_name);
    if (category) {
      rc = process_raw_category(raw_dir, category, pkt_name, wire, wire_len, &handler_ns);
    } else {
      fprintf(stderr, "skip: unknown packet %s\n", pkt_name);
      return 1;
    }
  }

  if (rc == 0) perf_add_packet(ps, handler_ns);
  return rc;
}

static int serve_client(int fd, const char *png_dir, const char *raw_dir, perf_stats *ps) {
  rx_buf rb;
  memset(&rb, 0, sizeof rb);
  uint8_t chunk[SOCK_READ_SZ];
  char pkt_name[PKT_NAME_MAX];

  for (;;) {
    perf_maybe_report(ps);

    ssize_t n = recv(fd, chunk, sizeof chunk, 0);
    if (n < 0) {
      if (errno == EINTR) continue;
      perror("recv");
      rx_buf_free(&rb);
      return -1;
    }
    if (n == 0) break;
    if (rx_buf_append(&rb, chunk, (size_t)n) != 0) {
      fprintf(stderr, "receive buffer overflow\n");
      rx_buf_free(&rb);
      return -1;
    }

    for (;;) {
      uint8_t *wire = NULL;
      size_t wire_len = 0;
      int got = rx_take_frame(&rb, pkt_name, sizeof pkt_name, &wire, &wire_len);
      if (got == 0) break;
      if (got < 0) {
        fprintf(stderr, "invalid frame\n");
        free(wire);
        rx_buf_free(&rb);
        return -1;
      }
      int rc = process_stream_packet(pkt_name, png_dir, raw_dir, wire, wire_len, ps);
      free(wire);
      if (rc < 0) {
        rx_buf_free(&rb);
        return -1;
      }
    }
  }

  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  perf_report_window(ps, &now);

  rx_buf_free(&rb);
  return 0;
}

static int parse_port(const char *s, int *out) {
  char *end = NULL;
  long v = strtol(s, &end, 10);
  if (!s[0] || (end && *end) || v <= 0 || v > 65535) return -1;
  *out = (int)v;
  return 0;
}

static int listen_tcp(const char *bind_host, int port) {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    perror("socket");
    return -1;
  }
  int one = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof addr);
  addr.sin_family = AF_INET;
  addr.sin_port = htons((uint16_t)port);
  if (inet_pton(AF_INET, bind_host, &addr.sin_addr) != 1) {
    fprintf(stderr, "invalid bind host: %s\n", bind_host);
    close(fd);
    return -1;
  }

  if (bind(fd, (struct sockaddr *)&addr, sizeof addr) != 0) {
    perror("bind");
    close(fd);
    return -1;
  }
  if (listen(fd, 1) != 0) {
    perror("listen");
    close(fd);
    return -1;
  }
  return fd;
}

static void default_raw_dir(const char *png_dir, char *out, size_t outlen) {
  snprintf(out, outlen, "%s/raw", png_dir);
}

static void usage(const char *prog) {
  fprintf(stderr,
          "Usage: %s [-v] <port> <png_dir> [raw_dir]\n"
          "       %s [-v] <bind_host> <port> <png_dir> [raw_dir]\n"
          "  -v, --verbose  log each processed packet (default: stats only)\n",
          prog, prog);
}

/** Collect -v/--verbose and return positional argc/argv (argv[0] unchanged). */
static int parse_cli(int argc, char **argv, int *pos_argc, const char **pos_argv) {
  int npos = 0;
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
      verbose_packets = 1;
      continue;
    }
    if (argv[i][0] == '-') {
      fprintf(stderr, "unknown option: %s\n", argv[i]);
      return -1;
    }
    if (npos >= POS_ARG_MAX) {
      fprintf(stderr, "too many arguments\n");
      return -1;
    }
    pos_argv[npos++] = argv[i];
  }
  *pos_argc = npos;
  return 0;
}

int main(int argc, char **argv) {
  const char *bind_host = "0.0.0.0";
  const char *png_dir;
  const char *raw_dir_arg = NULL;
  char raw_dir_buf[PATH_MAX_LC];
  int port;

  const char *pos[POS_ARG_MAX];
  int pos_argc = 0;
  if (parse_cli(argc, argv, &pos_argc, pos) != 0) {
    usage(argv[0]);
    return 1;
  }

  if (pos_argc == 2) {
    if (parse_port(pos[0], &port) != 0) {
      fprintf(stderr, "invalid port: %s\n", pos[0]);
      return 1;
    }
    png_dir = pos[1];
  } else if (pos_argc == 3) {
    int port2;
    if (parse_port(pos[0], &port) == 0 && parse_port(pos[1], &port2) != 0) {
      png_dir = pos[1];
      raw_dir_arg = pos[2];
    } else {
      bind_host = pos[0];
      if (parse_port(pos[1], &port) != 0) {
        fprintf(stderr, "invalid port: %s\n", pos[1]);
        return 1;
      }
      png_dir = pos[2];
    }
  } else if (pos_argc == 4) {
    bind_host = pos[0];
    if (parse_port(pos[1], &port) != 0) {
      fprintf(stderr, "invalid port: %s\n", pos[1]);
      return 1;
    }
    png_dir = pos[2];
    raw_dir_arg = pos[3];
  } else {
    usage(argv[0]);
    return 1;
  }

  const char *raw_dir;
  if (raw_dir_arg) {
    raw_dir = raw_dir_arg;
  } else {
    default_raw_dir(png_dir, raw_dir_buf, sizeof raw_dir_buf);
    raw_dir = raw_dir_buf;
  }

  if (mkdir_p(png_dir) != 0) {
    perror(png_dir);
    return 1;
  }
  if (mkdir_p(raw_dir) != 0) {
    perror(raw_dir);
    return 1;
  }

  int listen_fd = listen_tcp(bind_host, port);
  if (listen_fd < 0) return 1;

  fprintf(stderr, "chunk_stream_receiver %s:%d%s\n", bind_host, port,
          verbose_packets ? " (verbose)" : "");
  fprintf(stderr, "  png -> %s/rx*/rz*/cx*/cz*/x*_z*.png (map_chunk only)\n", png_dir);
  fprintf(stderr,
          "  raw -> %s/{map_chunk,block_change,multi_block_change,update_light,"
          "tile_entity_data,entity_*}/...\n",
          raw_dir);
  fprintf(stderr,
          "        coord.<packet>.wire; spawn_entity by block pos; other entity by id (overwrite); "
          "PNG only from map_chunk\n");

  perf_stats stats;
  perf_init(&stats);

  for (;;) {
    struct sockaddr_in peer;
    socklen_t peer_len = sizeof peer;
    int client = accept(listen_fd, (struct sockaddr *)&peer, &peer_len);
    if (client < 0) {
      if (errno == EINTR) continue;
      perror("accept");
      break;
    }

    char peer_addr[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &peer.sin_addr, peer_addr, sizeof peer_addr);
    fprintf(stderr, "client connected %s:%u\n", peer_addr, ntohs(peer.sin_port));

    perf_init(&stats);

    if (serve_client(client, png_dir, raw_dir, &stats) != 0) {
      close(client);
      break;
    }

    perf_session_summary(&stats);
    fprintf(stderr, "client disconnected\n");
    close(client);
  }

  close(listen_fd);
  return 0;
}

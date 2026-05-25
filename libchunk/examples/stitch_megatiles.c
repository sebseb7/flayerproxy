#define _POSIX_C_SOURCE 200809L

/**
 * Stitch per-chunk map PNGs (LC_MAP_CHUNK_PNG_SIZE, 2 px/block) into 16×16 megatiles.
 * Missing chunks stay black. Output names: x{tileWorldX}_z{tileWorldZ}.avif
 *
 * Usage: stitch_megatiles [-j N] <chunk_png_dir> [megatile_dir]
 *   megatile_dir defaults to <chunk_png_dir>/X16
 *   -j N  parallel workers (default: online CPU count)
 */
#include "libchunk.h"

/* RGB I/O (map_png.c / map_avif.c); not in public libchunk.h */
lc_status lc_read_rgb_png(const char *path, uint8_t **rgb, int *w, int *h);
lc_status lc_write_rgb_avif(const char *path, const uint8_t *rgb, int w, int h);

#include <dirent.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define TILE_CHUNKS LC_MAP_TILE_CHUNKS_PER_SIDE
#define CHUNK_PX LC_MAP_CHUNK_PNG_SIZE
#define TILE_PX LC_MAP_TILE_PNG_SIZE
/** Room for "/x<world>_z<world>.avif" after dir (GCC -Wformat-truncation). */
#define STITCH_DIR_MAX 4096
#define STITCH_PATH_SUFFIX 64
#define STITCH_PATH_MAX (STITCH_DIR_MAX + STITCH_PATH_SUFFIX)

typedef struct chunk_png_entry {
  int32_t wx;
  int32_t wz;
  int32_t cx;
  int32_t cz;
  int32_t tcx;
  int32_t tcz;
  char path[STITCH_PATH_MAX];
} chunk_png_entry;

typedef struct tile_job {
  int32_t tcx;
  int32_t tcz;
  size_t entry_lo;
  size_t entry_hi;
} tile_job;

typedef struct stitch_stats {
  int tiles_written;
  int tiles_skipped;
  int chunks_pasted;
  int read_errors;
  int write_errors;
} stitch_stats;

#define PROGRESS_SAMPLES 32

typedef struct progress_sample {
  size_t done;
  struct timespec t;
} progress_sample;

typedef struct progress_ctx {
  struct timespec start;
  progress_sample samples[PROGRESS_SAMPLES];
  int head;
  int count;
  uint64_t stitch_ns;
  uint64_t skip_ns;
  size_t stitch_jobs;
  size_t skip_jobs;
} progress_ctx;

typedef struct stitch_pool {
  const chunk_png_entry *entries;
  const char *out_dir;
  const tile_job *jobs;
  size_t num_jobs;
  int workers;
  _Atomic size_t next_job;
  _Atomic size_t jobs_done;
  progress_ctx progress;
  stitch_stats stats;
  pthread_mutex_t stats_mu;
  pthread_mutex_t progress_mu;
} stitch_pool;

static int stitch_path_join(char *out, size_t outlen, const char *dir, const char *leaf) {
  int dir_max = (int)(outlen > STITCH_PATH_SUFFIX ? outlen - STITCH_PATH_SUFFIX : 0);
  int n = snprintf(out, outlen, "%.*s/%s", dir_max, dir, leaf);
  return n >= 0 && (size_t)n < outlen;
}

static int32_t tile_origin_chunk(int32_t c) {
  if (c >= 0) return (c / TILE_CHUNKS) * TILE_CHUNKS;
  return ((c - (TILE_CHUNKS - 1)) / TILE_CHUNKS) * TILE_CHUNKS;
}

static int parse_chunk_png_name(const char *name, int32_t *wx, int32_t *wz) {
  if (name[0] != 'x') return 0;
  char *end = NULL;
  long x = strtol(name + 1, &end, 10);
  if (end == name + 1 || end[0] != '_' || end[1] != 'z') return 0;
  const char *zstart = end + 2;
  long z = strtol(zstart, &end, 10);
  if (end == zstart || strcmp(end, ".png") != 0) return 0;
  if ((x % 16) != 0 || (z % 16) != 0) return 0;
  *wx = (int32_t)x;
  *wz = (int32_t)z;
  return 1;
}

static int cmp_entries(const void *a, const void *b) {
  const chunk_png_entry *ea = (const chunk_png_entry *)a;
  const chunk_png_entry *eb = (const chunk_png_entry *)b;
  if (ea->tcx != eb->tcx) return (ea->tcx > eb->tcx) - (ea->tcx < eb->tcx);
  if (ea->tcz != eb->tcz) return (ea->tcz > eb->tcz) - (ea->tcz < eb->tcz);
  if (ea->cz != eb->cz) return (ea->cz > eb->cz) - (ea->cz < eb->cz);
  return (ea->cx > eb->cx) - (ea->cx < eb->cx);
}

static int mkdir_p(const char *path) {
  char buf[STITCH_DIR_MAX];
  snprintf(buf, sizeof buf, "%.*s", (int)(sizeof buf - 1), path);
  for (char *p = buf + 1; *p; p++) {
    if (*p != '/') continue;
    *p = '\0';
    if (mkdir(buf, 0755) != 0 && errno != EEXIST) return -1;
    *p = '/';
  }
  return mkdir(buf, 0755) == 0 || errno == EEXIST ? 0 : -1;
}

static double timespec_elapsed_sec(const struct timespec *start, const struct timespec *now) {
  return (double)(now->tv_sec - start->tv_sec) +
         (double)(now->tv_nsec - start->tv_nsec) / 1e9;
}

static void format_duration(double sec, char *buf, size_t buflen) {
  if (sec < 0 || sec != sec || sec > 86400.0 * 7) {
    snprintf(buf, buflen, "?");
    return;
  }
  if (sec < 1.0) {
    snprintf(buf, buflen, "<1s");
    return;
  }
  int s = (int)(sec + 0.5);
  if (s < 60) {
    snprintf(buf, buflen, "%ds", s);
    return;
  }
  int m = s / 60;
  s %= 60;
  if (m < 60) {
    snprintf(buf, buflen, "%dm%02ds", m, s);
    return;
  }
  int h = m / 60;
  m %= 60;
  snprintf(buf, buflen, "%dh%02dm", h, m);
}

static int tile_output_path(char *path, size_t pathlen, const char *out_dir, int32_t tcx,
                            int32_t tcz) {
  int32_t twx = tcx * 16;
  int32_t twz = tcz * 16;
  int n = snprintf(path, pathlen, "%.*s/x%d_z%d.avif",
                   (int)(pathlen - STITCH_PATH_SUFFIX), out_dir, twx, twz);
  return n >= 0 && (size_t)n < pathlen;
}

static int file_mtime(const char *path, struct timespec *out) {
  struct stat st;
  if (stat(path, &st) != 0) return -1;
#if defined(_POSIX_VERSION) && _POSIX_VERSION >= 200809L
  out->tv_sec = st.st_mtim.tv_sec;
  out->tv_nsec = st.st_mtim.tv_nsec;
#else
  out->tv_sec = st.st_mtime;
  out->tv_nsec = 0;
#endif
  return 0;
}

static int timespec_le(const struct timespec *a, const struct timespec *b) {
  if (a->tv_sec != b->tv_sec) return a->tv_sec < b->tv_sec;
  return a->tv_nsec <= b->tv_nsec;
}

/** 1 if output exists and every chunk PNG is not newer than the output. */
static int tile_output_up_to_date(const char *out_dir, int32_t tcx, int32_t tcz,
                                  const chunk_png_entry *entries, size_t lo, size_t hi) {
  char out_path[STITCH_PATH_MAX];
  if (!tile_output_path(out_path, sizeof out_path, out_dir, tcx, tcz)) return 0;

  struct timespec out_mtime;
  if (file_mtime(out_path, &out_mtime) != 0) return 0;

  for (size_t i = lo; i < hi; i++) {
    struct timespec in_mtime;
    if (file_mtime(entries[i].path, &in_mtime) != 0) return 0;
    if (!timespec_le(&in_mtime, &out_mtime)) return 0;
  }
  return 1;
}

static void progress_record_job(progress_ctx *ctx, size_t done, int skipped, double job_sec) {
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  ctx->samples[ctx->head] = (progress_sample){done, now};
  ctx->head = (ctx->head + 1) % PROGRESS_SAMPLES;
  if (ctx->count < PROGRESS_SAMPLES) ctx->count++;

  uint64_t ns = (uint64_t)(job_sec * 1e9);
  if (skipped) {
    ctx->skip_ns += ns;
    ctx->skip_jobs++;
  } else {
    ctx->stitch_ns += ns;
    ctx->stitch_jobs++;
  }
}

/** Jobs/sec over the best sample window in the last ~3–10s (ignores early skip bursts). */
static double progress_recent_rate(const progress_ctx *ctx, size_t done,
                                   const struct timespec *now) {
  double best_rate = 0;
  double best_span = 0;
  for (int i = 0; i < ctx->count; i++) {
    int idx = (ctx->head - 1 - i + PROGRESS_SAMPLES) % PROGRESS_SAMPLES;
    const progress_sample *s = &ctx->samples[idx];
    if (s->done >= done) continue;
    double span = timespec_elapsed_sec(&s->t, now);
    if (span < 2.0) continue;
    double rate = (double)(done - s->done) / span;
    if (span >= 3.0 && span <= 12.0) {
      if (span > best_span || best_span < 3.0) {
        best_span = span;
        best_rate = rate;
      }
    } else if (best_span < 3.0) {
      if (span > best_span) {
        best_span = span;
        best_rate = rate;
      }
    }
  }
  return best_rate;
}

static double progress_eta_sec(const progress_ctx *ctx, size_t done, size_t total, int workers,
                               const struct timespec *now) {
  size_t remain = total - done;
  if (remain == 0) return 0;

  double elapsed = timespec_elapsed_sec(&ctx->start, now);
  double eta = -1;

  double recent = progress_recent_rate(ctx, done, now);
  if (recent > 0) eta = (double)remain / recent;
  else if (done > 0 && elapsed > 0) eta = (double)remain * elapsed / (double)done;

  /* Skips are near-instant; floor assumes remaining tiles may all need a full stitch. */
  if (ctx->stitch_jobs > 0 && workers > 0) {
    double avg_stitch = (double)ctx->stitch_ns / 1e9 / (double)ctx->stitch_jobs;
    double stitch_floor = (double)remain * avg_stitch / (double)workers;
    if (eta < 0 || eta < stitch_floor) eta = stitch_floor;
  }

  return eta;
}

static void print_progress(progress_ctx *ctx, size_t done, size_t total, int workers) {
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  double elapsed = timespec_elapsed_sec(&ctx->start, &now);
  double pct = total ? 100.0 * (double)done / (double)total : 100.0;
  char elapsed_s[32];
  char eta_s[32];
  format_duration(elapsed, elapsed_s, sizeof elapsed_s);
  if (done == 0 || done >= total) {
    snprintf(eta_s, sizeof eta_s, done >= total ? "0s" : "?");
  } else {
    double eta = progress_eta_sec(ctx, done, total, workers, &now);
    if (eta < 0) snprintf(eta_s, sizeof eta_s, "?");
    else format_duration(eta, eta_s, sizeof eta_s);
  }
  fprintf(stderr, "\rprogress: %zu/%zu (%.1f%%) elapsed %s eta %s   ", done, total, pct,
          elapsed_s, eta_s);
  fflush(stderr);
}

static lc_status paste_chunk_rgb(uint8_t *tile_rgb, const uint8_t *chunk_rgb, int local_cx,
                                 int local_cz) {
  if (local_cx < 0 || local_cx >= TILE_CHUNKS || local_cz < 0 || local_cz >= TILE_CHUNKS) {
    return LC_ERR_INVALID;
  }
  int dst_x0 = local_cx * CHUNK_PX;
  int dst_z0 = local_cz * CHUNK_PX;
  for (int row = 0; row < CHUNK_PX; row++) {
    const uint8_t *src = chunk_rgb + (size_t)row * (size_t)CHUNK_PX * 3;
    uint8_t *dst = tile_rgb + ((size_t)(dst_z0 + row) * (size_t)TILE_PX + (size_t)dst_x0) * 3;
    memcpy(dst, src, (size_t)CHUNK_PX * 3);
  }
  return LC_OK;
}

static lc_status write_tile(const char *out_dir, int32_t tcx, int32_t tcz, const uint8_t *rgb) {
  char path[STITCH_PATH_MAX];
  int32_t twx = tcx * 16;
  int32_t twz = tcz * 16;
  int n = snprintf(path, sizeof path, "%.*s/x%d_z%d.avif",
                   (int)(sizeof path - STITCH_PATH_SUFFIX), out_dir, twx, twz);
  if (n < 0 || (size_t)n >= sizeof path) return LC_ERR_INVALID;
  return lc_write_rgb_avif(path, rgb, TILE_PX, TILE_PX);
}

static void stats_add(stitch_pool *pool, int tiles, int skipped, int pasted, int read_err,
                      int write_err) {
  pthread_mutex_lock(&pool->stats_mu);
  pool->stats.tiles_written += tiles;
  pool->stats.tiles_skipped += skipped;
  pool->stats.chunks_pasted += pasted;
  pool->stats.read_errors += read_err;
  pool->stats.write_errors += write_err;
  pthread_mutex_unlock(&pool->stats_mu);
}

static void progress_done(stitch_pool *pool, int skipped, double job_sec) {
  size_t done = atomic_fetch_add(&pool->jobs_done, 1) + 1;
  pthread_mutex_lock(&pool->progress_mu);
  progress_record_job(&pool->progress, done, skipped, job_sec);
  print_progress(&pool->progress, done, pool->num_jobs, pool->workers);
  pthread_mutex_unlock(&pool->progress_mu);
}

static stitch_stats stitch_one_tile(const char *out_dir, int32_t tcx, int32_t tcz,
                                    const chunk_png_entry *entries, size_t lo, size_t hi) {
  stitch_stats s = {0, 0, 0, 0, 0};
  if (tile_output_up_to_date(out_dir, tcx, tcz, entries, lo, hi)) {
    s.tiles_skipped = 1;
    return s;
  }
  size_t tile_bytes = (size_t)TILE_PX * (size_t)TILE_PX * 3;
  uint8_t *tile_rgb = (uint8_t *)malloc(tile_bytes);
  if (!tile_rgb) {
    s.write_errors = 1;
    return s;
  }
  memset(tile_rgb, 0, tile_bytes);

  for (size_t i = lo; i < hi; i++) {
    int w = 0, h = 0;
    uint8_t *chunk_rgb = NULL;
    if (lc_read_rgb_png(entries[i].path, &chunk_rgb, &w, &h) != LC_OK || w != CHUNK_PX ||
        h != CHUNK_PX) {
      fprintf(stderr, "skip (bad png): %s\n", entries[i].path);
      s.read_errors++;
      free(chunk_rgb);
      continue;
    }

    int local_cx = entries[i].cx - tcx;
    int local_cz = entries[i].cz - tcz;
    if (paste_chunk_rgb(tile_rgb, chunk_rgb, local_cx, local_cz) != LC_OK) {
      fprintf(stderr, "skip (out of tile): %s\n", entries[i].path);
    } else {
      s.chunks_pasted++;
    }
    free(chunk_rgb);
  }

  if (write_tile(out_dir, tcx, tcz, tile_rgb) != LC_OK) {
    fprintf(stderr, "write error: x%d_z%d.avif\n", tcx * 16, tcz * 16);
    s.write_errors = 1;
  } else {
    s.tiles_written = 1;
  }
  free(tile_rgb);
  return s;
}

static void *stitch_worker(void *arg) {
  stitch_pool *pool = (stitch_pool *)arg;
  for (;;) {
    size_t j = atomic_fetch_add(&pool->next_job, 1);
    if (j >= pool->num_jobs) break;
    const tile_job *job = &pool->jobs[j];
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    stitch_stats s =
        stitch_one_tile(pool->out_dir, job->tcx, job->tcz, pool->entries, job->entry_lo,
                        job->entry_hi);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    stats_add(pool, s.tiles_written, s.tiles_skipped, s.chunks_pasted, s.read_errors,
              s.write_errors);
    progress_done(pool, s.tiles_skipped, timespec_elapsed_sec(&t0, &t1));
  }
  return NULL;
}

static int default_jobs(void) {
  long n = sysconf(_SC_NPROCESSORS_ONLN);
  return n > 0 ? (int)n : 1;
}

static int parse_jobs_arg(const char *arg) {
  if (!arg || !arg[0]) return -1;
  char *end = NULL;
  long v = strtol(arg, &end, 10);
  if (end == arg || (end && *end) || v < 1 || v > 4096) return -1;
  return (int)v;
}

static void usage(const char *prog) {
  fprintf(stderr, "Usage: %s [-j N] <chunk_png_dir> [megatile_dir]\n", prog);
  fprintf(stderr,
          "  Stitch %dx%d chunk PNGs into %dx%d megatiles (16 chunks/side, black gaps).\n",
          CHUNK_PX, CHUNK_PX, TILE_PX, TILE_PX);
  fprintf(stderr, "  -j N  parallel workers (default: CPU count)\n");
}

int main(int argc, char **argv) {
  int jobs = 0;
  int argi = 1;
  while (argi < argc && argv[argi][0] == '-') {
    if (strcmp(argv[argi], "-j") == 0) {
      if (argi + 1 >= argc) {
        usage(argv[0]);
        return 1;
      }
      jobs = parse_jobs_arg(argv[argi + 1]);
      if (jobs < 0) {
        fprintf(stderr, "invalid -j value: %s\n", argv[argi + 1]);
        return 1;
      }
      argi += 2;
      continue;
    }
    if (strncmp(argv[argi], "-j", 2) == 0 && argv[argi][2]) {
      jobs = parse_jobs_arg(argv[argi] + 2);
      if (jobs < 0) {
        fprintf(stderr, "invalid -j value: %s\n", argv[argi] + 2);
        return 1;
      }
      argi++;
      continue;
    }
    usage(argv[0]);
    return 1;
  }

  if (jobs <= 0) jobs = default_jobs();

  if (argi >= argc || argc - argi > 2) {
    usage(argv[0]);
    return 1;
  }

  const char *in_dir = argv[argi];
  char out_dir_buf[STITCH_DIR_MAX + 8];
  const char *out_dir;
  if (argc - argi == 2) {
    out_dir = argv[argi + 1];
  } else {
    snprintf(out_dir_buf, sizeof out_dir_buf, "%.*s/X16",
             (int)(sizeof out_dir_buf - (sizeof "/X16" - 1) - 1), in_dir);
    out_dir = out_dir_buf;
  }

  DIR *d = opendir(in_dir);
  if (!d) {
    perror(in_dir);
    return 1;
  }

  chunk_png_entry *entries = NULL;
  size_t n = 0, cap = 0;
  struct dirent *ent;
  while ((ent = readdir(d)) != NULL) {
    if (ent->d_name[0] == '.') continue;
    int32_t wx, wz;
    if (!parse_chunk_png_name(ent->d_name, &wx, &wz)) continue;

    if (n == cap) {
      cap = cap ? cap * 2 : 256;
      chunk_png_entry *next =
          (chunk_png_entry *)realloc(entries, cap * sizeof(chunk_png_entry));
      if (!next) {
        closedir(d);
        free(entries);
        fprintf(stderr, "oom\n");
        return 1;
      }
      entries = next;
    }

    chunk_png_entry *e = &entries[n++];
    e->wx = wx;
    e->wz = wz;
    e->cx = wx / 16;
    e->cz = wz / 16;
    e->tcx = tile_origin_chunk(e->cx);
    e->tcz = tile_origin_chunk(e->cz);
    if (!stitch_path_join(e->path, sizeof e->path, in_dir, ent->d_name)) {
      fprintf(stderr, "skip (path too long): %s/%s\n", in_dir, ent->d_name);
      n--;
      continue;
    }
  }
  closedir(d);

  if (n == 0) {
    free(entries);
    fprintf(stderr, "no chunk PNGs matching x<world>_z<world>.png in %s\n", in_dir);
    return 1;
  }

  qsort(entries, n, sizeof(entries[0]), cmp_entries);

  if (mkdir_p(out_dir) != 0) {
    perror(out_dir);
    free(entries);
    return 1;
  }

  size_t num_tiles = 0;
  for (size_t i = 0; i < n;) {
    size_t j = i + 1;
    while (j < n && entries[j].tcx == entries[i].tcx && entries[j].tcz == entries[i].tcz) j++;
    num_tiles++;
    i = j;
  }

  tile_job *tile_jobs = (tile_job *)malloc(num_tiles * sizeof(tile_job));
  if (!tile_jobs) {
    free(entries);
    fprintf(stderr, "oom\n");
    return 1;
  }
  size_t t = 0;
  for (size_t i = 0; i < n;) {
    size_t j = i + 1;
    while (j < n && entries[j].tcx == entries[i].tcx && entries[j].tcz == entries[i].tcz) j++;
    tile_jobs[t++] = (tile_job){entries[i].tcx, entries[i].tcz, i, j};
    i = j;
  }

  stitch_stats total = {0, 0, 0, 0, 0};
  int workers = jobs;
  if (workers > (int)num_tiles) workers = (int)num_tiles;
  if (workers < 1) workers = 1;

  fprintf(stderr, "stitching %zu megatile(s) with %d worker(s)...\n", num_tiles, workers);
  progress_ctx prog = {0};
  clock_gettime(CLOCK_MONOTONIC, &prog.start);
  print_progress(&prog, 0, num_tiles, workers);

  if (workers == 1) {
    for (size_t i = 0; i < num_tiles; i++) {
      const tile_job *job = &tile_jobs[i];
      struct timespec t0, t1;
      clock_gettime(CLOCK_MONOTONIC, &t0);
      stitch_stats s = stitch_one_tile(out_dir, job->tcx, job->tcz, entries, job->entry_lo,
                                       job->entry_hi);
      clock_gettime(CLOCK_MONOTONIC, &t1);
      total.tiles_written += s.tiles_written;
      total.tiles_skipped += s.tiles_skipped;
      total.chunks_pasted += s.chunks_pasted;
      total.read_errors += s.read_errors;
      total.write_errors += s.write_errors;
      progress_record_job(&prog, i + 1, s.tiles_skipped, timespec_elapsed_sec(&t0, &t1));
      print_progress(&prog, i + 1, num_tiles, workers);
    }
  } else {
    stitch_pool pool = {
        .entries = entries,
        .out_dir = out_dir,
        .jobs = tile_jobs,
        .num_jobs = num_tiles,
        .workers = workers,
        .next_job = 0,
        .jobs_done = 0,
        .progress = prog,
        .stats = {0, 0, 0, 0, 0},
    };
    pthread_mutex_init(&pool.stats_mu, NULL);
    pthread_mutex_init(&pool.progress_mu, NULL);

    pthread_t *threads = (pthread_t *)malloc((size_t)workers * sizeof(pthread_t));
    if (!threads) {
      pthread_mutex_destroy(&pool.stats_mu);
      pthread_mutex_destroy(&pool.progress_mu);
      free(tile_jobs);
      free(entries);
      fprintf(stderr, "oom\n");
      return 1;
    }
    for (int w = 0; w < workers; w++) {
      if (pthread_create(&threads[w], NULL, stitch_worker, &pool) != 0) {
        fprintf(stderr, "pthread_create failed\n");
        for (int k = 0; k < w; k++) pthread_join(threads[k], NULL);
        pthread_mutex_destroy(&pool.stats_mu);
        pthread_mutex_destroy(&pool.progress_mu);
        free(threads);
        free(tile_jobs);
        free(entries);
        return 1;
      }
    }
    for (int w = 0; w < workers; w++) pthread_join(threads[w], NULL);
    pthread_mutex_destroy(&pool.stats_mu);
    pthread_mutex_destroy(&pool.progress_mu);
    free(threads);
    total = pool.stats;
  }

  fprintf(stderr, "\n");
  free(tile_jobs);
  free(entries);

  fprintf(stderr,
          "wrote %d megatile(s), skipped %d up-to-date (%dx%d px) to %s from %zu chunk png(s) "
          "(%d worker(s))",
          total.tiles_written, total.tiles_skipped, TILE_PX, TILE_PX, out_dir, n, workers);
  if (total.read_errors) fprintf(stderr, ", %d read error(s)", total.read_errors);
  fprintf(stderr, " (%d pasted)\n", total.chunks_pasted);
  if (total.write_errors) return 1;
  return total.read_errors ? 1 : 0;
}

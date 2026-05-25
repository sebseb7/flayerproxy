#define _POSIX_C_SOURCE 200809L

/**
 * List the 256 map-PNG columns for map_chunk wire captures (libchunk decoder).
 *
 * Uses lc_map_chunk_fprint_map_surface — same top-block + map color logic as
 * lc_map_chunk_write_top_png / X16 tile blending.
 *
 * Usage:
 *   list_map_surface <raw-wire-file> [more ...]
 *   list_map_surface --dir <input_dir> [--chunk CX CZ]
 */
#include "libchunk.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WIRE_MAX (16 * 1024 * 1024)

static int parse_map_chunk_basename(const char *basename) {
  const char *p = basename;
  while (*p >= '0' && *p <= '9') p++;
  if (*p != '-') return 0;
  p++;
  if (strncmp(p, "map_chunk", 9) != 0) return 0;
  if (p[9] == '\0') return 1;
  if (p[9] == '-') {
    for (p += 10; *p; p++) {
      if (*p < '0' || *p > '9') return 0;
    }
    return 1;
  }
  return 0;
}

static int read_wire_file(const char *path, uint8_t **wire, size_t *len) {
  FILE *f = fopen(path, "rb");
  if (!f) {
    perror(path);
    return -1;
  }
  if (fseek(f, 0, SEEK_END) != 0) {
    fclose(f);
    return -1;
  }
  long sz = ftell(f);
  if (sz <= 0 || sz > (long)WIRE_MAX) {
    fprintf(stderr, "%s: invalid size %ld\n", path, sz);
    fclose(f);
    return -1;
  }
  if (fseek(f, 0, SEEK_SET) != 0) {
    fclose(f);
    return -1;
  }
  uint8_t *buf = (uint8_t *)malloc((size_t)sz);
  if (!buf) {
    fclose(f);
    return -1;
  }
  if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
    free(buf);
    fclose(f);
    return -1;
  }
  fclose(f);
  *wire = buf;
  *len = (size_t)sz;
  return 0;
}

static int process_wire(const char *label, const uint8_t *wire, size_t wire_len, int32_t want_cx,
                        int32_t want_cz, int have_filter) {
  const uint8_t *payload = wire;
  size_t payload_len = wire_len;
  if (lc_skip_packet_id(wire, wire_len, &payload, &payload_len) != LC_OK) {
    fprintf(stderr, "%s: skip packet id failed\n", label);
    return 1;
  }

  lc_map_chunk mc;
  memset(&mc, 0, sizeof mc);
  lc_status st = lc_parse_map_chunk(payload, payload_len, &mc);
  if (st != LC_OK) {
    fprintf(stderr, "%s: parse error\n", label);
    return 1;
  }

  if (have_filter && (mc.x != want_cx || mc.z != want_cz)) {
    lc_map_chunk_free(&mc);
    return 0;
  }

  printf("# %s\n", label);
  if (lc_map_chunk_fprint_map_surface(stdout, &mc) != 0) {
    fprintf(stderr, "%s: surface listing failed\n", label);
    lc_map_chunk_free(&mc);
    return 1;
  }
  fputc('\n', stdout);
  lc_map_chunk_free(&mc);
  return 0;
}

static int process_path(const char *path, int32_t want_cx, int32_t want_cz, int have_filter) {
  const char *base = strrchr(path, '/');
  base = base ? base + 1 : path;
  if (!parse_map_chunk_basename(base)) {
    fprintf(stderr, "skip (not map_chunk): %s\n", path);
    return 0;
  }

  uint8_t *wire = NULL;
  size_t len = 0;
  if (read_wire_file(path, &wire, &len) != 0) return 1;
  int rc = process_wire(path, wire, len, want_cx, want_cz, have_filter);
  free(wire);
  return rc;
}

static int process_dir(const char *dir, int32_t want_cx, int32_t want_cz, int have_filter) {
  DIR *d = opendir(dir);
  if (!d) {
    perror(dir);
    return 1;
  }

  int err = 0;
  struct dirent *ent;
  while ((ent = readdir(d)) != NULL) {
    if (ent->d_name[0] == '.') continue;
    if (!parse_map_chunk_basename(ent->d_name)) continue;

    char path[4096];
    snprintf(path, sizeof path, "%s/%s", dir, ent->d_name);
    if (process_path(path, want_cx, want_cz, have_filter) != 0) err = 1;
  }
  closedir(d);
  return err;
}

int main(int argc, char **argv) {
  const char *dir = NULL;
  int32_t want_cx = 0, want_cz = 0;
  int have_filter = 0;

  int i = 1;
  while (i < argc) {
    if (strcmp(argv[i], "--dir") == 0 && i + 1 < argc) {
      dir = argv[++i];
      i++;
      continue;
    }
    if (strcmp(argv[i], "--chunk") == 0 && i + 2 < argc) {
      want_cx = (int32_t)atoi(argv[++i]);
      want_cz = (int32_t)atoi(argv[++i]);
      have_filter = 1;
      i++;
      continue;
    }
    break;
  }

  if (dir) {
    if (i < argc) {
      fprintf(stderr, "usage: %s --dir <dir> [--chunk CX CZ]\n", argv[0]);
      return 2;
    }
    return process_dir(dir, want_cx, want_cz, have_filter) != 0 ? 1 : 0;
  }

  if (i >= argc) {
    fprintf(stderr,
            "usage: %s <map_chunk-wire-file> ...\n"
            "       %s --dir <dir> [--chunk CX CZ]\n",
            argv[0], argv[0]);
    return 2;
  }

  int err = 0;
  for (; i < argc; i++) {
    if (process_path(argv[i], want_cx, want_cz, have_filter) != 0) err = 1;
  }
  return err ? 1 : 0;
}

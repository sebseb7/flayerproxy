/**
 * Compare two mc_reference_client capture directories (index.txt + .wire files).
 *
 * Usage: compare_join_capture <ref_dir> <test_dir>
 */
#include "decode_wire.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LINE_MAX 1024
#define MAX_ROWS 512

typedef struct {
  int seq;
  char phase[16];
  char id_hex[8];
  char name[64];
  size_t len;
  char path[512];
} cap_row;

static int load_index(const char *dir, cap_row *rows, int max_rows) {
  char path[600];
  snprintf(path, sizeof path, "%s/index.txt", dir);
  FILE *f = fopen(path, "r");
  if (!f) {
    perror(path);
    return -1;
  }
  int n = 0;
  char line[LINE_MAX];
  while (n < max_rows && fgets(line, sizeof line, f)) {
    if (line[0] == '#') continue;
    cap_row *r = &rows[n];
    char dir[8];
    if (sscanf(line, "%d %7s %15s 0x%7s %63s %zu %511s", &r->seq, dir, r->phase, r->id_hex, r->name, &r->len,
               r->path) >= 6) {
      if (strcmp(dir, "s2c") != 0) continue;
      n++;
    } else if (sscanf(line, "%d %15s 0x%7s %63s %zu %511s", &r->seq, r->phase, r->id_hex, r->name, &r->len,
                        r->path) >= 5) {
      n++;
    }
  }
  fclose(f);
  return n;
}

static int read_file(const char *path, uint8_t **out, size_t *out_len) {
  FILE *f = fopen(path, "rb");
  if (!f) return -1;
  if (fseek(f, 0, SEEK_END) != 0) {
    fclose(f);
    return -1;
  }
  long sz = ftell(f);
  if (sz < 0) {
    fclose(f);
    return -1;
  }
  rewind(f);
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
  *out = buf;
  *out_len = (size_t)sz;
  return 0;
}

static void hex_diff(const uint8_t *a, size_t a_len, const uint8_t *b, size_t b_len) {
  size_t n = a_len < b_len ? a_len : b_len;
  for (size_t i = 0; i < n; i++) {
    if (a[i] != b[i]) {
      printf("    first mismatch at offset %zu: ref 0x%02x test 0x%02x\n", i, a[i], b[i]);
      return;
    }
  }
  if (a_len != b_len) printf("    lengths differ: ref %zu test %zu (prefix matched %zu)\n", a_len, b_len, n);
}

static int try_parse_map_chunk(const char *label, const uint8_t *wire, size_t wire_len) {
  if (strcmp(label, "map_chunk") != 0) return 0;
  char buf[8192];
  int rc = lc_decode_wire_to_string("map_chunk", wire, wire_len, buf, sizeof buf);
  if (rc > 0) {
    printf("    libchunk parse %s: OK (%zu chars summary)\n", label, strlen(buf));
    return 0;
  }
  printf("    libchunk parse %s: FAILED\n", label);
  return -1;
}

int main(int argc, char **argv) {
  if (argc != 3) {
    fprintf(stderr, "Usage: %s <reference_dir> <test_dir>\n", argv[0]);
    return 1;
  }
  cap_row ref[MAX_ROWS], test[MAX_ROWS];
  int nref = load_index(argv[1], ref, MAX_ROWS);
  int ntest = load_index(argv[2], test, MAX_ROWS);
  if (nref < 0 || ntest < 0) return 1;

  printf("Reference: %d packets (%s)\n", nref, argv[1]);
  printf("Test:      %d packets (%s)\n\n", ntest, argv[2]);

  int play_ref = 0, play_test = 0;
  for (int i = 0; i < nref; i++)
    if (strcmp(ref[i].phase, "play") == 0) play_ref++;
  for (int i = 0; i < ntest; i++)
    if (strcmp(test[i].phase, "play") == 0) play_test++;

  printf("Play phase: ref %d packets, test %d packets\n\n", play_ref, play_test);

  printf("%-4s %-8s %-6s %-24s %8s %8s\n", "#", "phase", "id", "name", "ref_len", "test_len");
  int i = 0, j = 0, mismatches = 0;
  while (i < nref || j < ntest) {
    const cap_row *a = i < nref ? &ref[i] : NULL;
    const cap_row *b = j < ntest ? &test[j] : NULL;
    int cmp = 0;
    if (a && b) {
      cmp = strcmp(a->name, b->name);
      if (cmp == 0 && strcmp(a->phase, b->phase) == 0) {
        size_t al = a->len, bl = b->len;
        const char mark = (al == bl) ? ' ' : '!';
        if (al != bl) mismatches++;
        printf("%c %-4d %-8s 0x%-4s %-24s %8zu %8zu\n", mark, a->seq, a->phase, a->id_hex, a->name, al, bl);
        if (al != bl || strcmp(a->name, "map_chunk") == 0 || strcmp(a->name, "login") == 0) {
          uint8_t *wa = NULL, *wb = NULL;
          size_t la = 0, lb = 0;
          if (read_file(a->path, &wa, &la) == 0 && read_file(b->path, &wb, &lb) == 0) {
            if (al != bl) hex_diff(wa, la, wb, lb);
            try_parse_map_chunk(a->name, wa, la);
            try_parse_map_chunk(a->name, wb, lb);
          }
          free(wa);
          free(wb);
        }
        i++;
        j++;
        continue;
      }
    }
    if (!b || (a && (cmp < 0 || strcmp(a->phase, b->phase) < 0))) {
      printf("< ref only: %s 0x%s %s %zu\n", a->phase, a->id_hex, a->name, a->len);
      mismatches++;
      i++;
    } else {
      printf("> test only: %s 0x%s %s %zu\n", b->phase, b->id_hex, b->name, b->len);
      mismatches++;
      j++;
    }
  }

  int ref_mc = 0, test_mc = 0;
  for (int k = 0; k < nref; k++)
    if (strcmp(ref[k].name, "map_chunk") == 0) ref_mc++;
  for (int k = 0; k < ntest; k++)
    if (strcmp(test[k].name, "map_chunk") == 0) test_mc++;
  printf("\nmap_chunk count: ref %d test %d (expect 25 for view-distance=2)\n", ref_mc, test_mc);
  printf("sequence mismatches / size diffs: %d\n", mismatches);
  return mismatches > 0 ? 1 : 0;
}

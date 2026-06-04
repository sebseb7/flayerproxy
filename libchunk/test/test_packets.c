#include "libchunk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int hex_nibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

static int hex_to_bytes(const char *hex, uint8_t **out, size_t *out_len) {
  size_t n = strlen(hex);
  if (n % 2) return -1;
  *out_len = n / 2;
  *out = (uint8_t *)malloc(*out_len);
  if (!*out) return -1;
  for (size_t i = 0; i < *out_len; i++) {
    int hi = hex_nibble(hex[i * 2]);
    int lo = hex_nibble(hex[i * 2 + 1]);
    if (hi < 0 || lo < 0) {
      free(*out);
      return -1;
    }
    (*out)[i] = (uint8_t)((hi << 4) | lo);
  }
  return 0;
}

static int run_case(const char *name, const char *hex,
                    lc_status (*parse)(const uint8_t *, size_t, void *),
                    void (*free_fn)(void *), int (*fmt)(const void *, char *, size_t),
                    size_t struct_size) {
  uint8_t *p = NULL;
  size_t n = 0;
  if (hex_to_bytes(hex, &p, &n) != 0) return -1;
  void *pkt = calloc(1, struct_size);
  if (!pkt) {
    free(p);
    return -1;
  }
  lc_status st = parse(p, n, pkt);
  free(p);
  if (st != LC_OK) {
    fprintf(stderr, "%s parse failed: %d\n", name, (int)st);
    free(pkt);
    return -1;
  }
  char line[2048];
  fmt(pkt, line, sizeof(line));
  printf("OK %s\n  %s\n", name, line);
  if (free_fn) free_fn(pkt);
  free(pkt);
  return 0;
}

int main(void) {
  int failures = 0;

#define CASE(name, type, parse, free_fn, to_str, hex) \
  if (run_case(name, hex, (lc_status (*)(const uint8_t *, size_t, void *))parse, \
               (void (*)(void *))free_fn, (int (*)(const void *, char *, size_t))to_str, \
               sizeof(type)) != 0) \
    failures++

  CASE("block_change", lc_block_change, lc_parse_block_change, NULL, lc_block_change_to_string,
       "ffffffc00000206400");
  CASE("spawn_entity", lc_spawn_entity, lc_parse_spawn_entity, NULL, lc_spawn_entity_to_string,
       "96819604fd0613350c93481486075dbbce00f83e2c4050e00000000000405300000000000040572000000000000000000000");
  CASE("entity_metadata", lc_entity_metadata, lc_parse_entity_metadata, lc_entity_metadata_free,
       lc_entity_metadata_to_string, "99de9a070903418078391201e9a90410007fff");
  CASE("entity_metadata", lc_entity_metadata, lc_parse_entity_metadata, lc_entity_metadata_free,
       lc_entity_metadata_to_string,
       "8cd8e30402060108001a587261795f446f632069732061206e6967676572666167676f74090343960000ff");
  CASE("entity_equipment", lc_entity_equipment, lc_parse_entity_equipment, lc_entity_equipment_free,
       lc_entity_equipment_to_string, "93fce6040001fe0601000a011902");
  CASE("multi_block_change", lc_multi_block_change, lc_parse_multi_block_change,
       lc_multi_block_change_free, lc_multi_block_change_to_string, "fffffc00000000060294189518");
  CASE("initialize_world_border", lc_initialize_world_border, lc_parse_initialize_world_border, NULL,
       lc_initialize_world_border_to_string,
       "00000000000000000000000000000000418c9c3700000000418c9c370000000000f086a70e050f");
  CASE("rel_entity_move", lc_rel_entity_move, lc_parse_rel_entity_move, NULL, lc_rel_entity_move_to_string,
       "fca0990700000000000001");
  CASE("entity_destroy", lc_entity_destroy, lc_parse_entity_destroy, lc_entity_destroy_free,
       lc_entity_destroy_to_string, "01b9899a07");
  CASE("set_passengers", lc_set_passengers, lc_parse_set_passengers, lc_set_passengers_free,
       lc_set_passengers_to_string, "de84960401df849604");
  CASE("entity_velocity", lc_entity_velocity, lc_parse_entity_velocity, NULL, lc_entity_velocity_to_string,
       "0af9ff81ebfe71");
  CASE("entity_head_rotation", lc_entity_head_rotation, lc_parse_entity_head_rotation, NULL,
       lc_entity_head_rotation_to_string, "a0c29706db");

#undef CASE

  {
    const char *sign_hex =
        "0a00000a000a66726f6e745f746578740900086d657373616765730a0000000408000474"
        "65787400027474000000000000";
    uint8_t *bytes = NULL;
    size_t blen = 0;
    if (hex_to_bytes(sign_hex, &bytes, &blen) != 0) {
      fprintf(stderr, "sign_text: hex decode failed\n");
      failures++;
    } else {
      lc_sign_text st;
      memset(&st, 0, sizeof st);
      if (lc_sign_text_from_nbt(bytes, blen, &st) != LC_OK || !st.has_sign) {
        fprintf(stderr, "sign_text: parse failed\n");
        failures++;
      } else if (!st.front[0] || strcmp(st.front[0], "tt") != 0) {
        fprintf(stderr, "sign_text: expected front[0]=tt got %s\n",
                st.front[0] ? st.front[0] : "(null)");
        failures++;
      }
      lc_sign_text_free(&st);
      free(bytes);
    }
  }

  {
    /* Test new parsers */
    uint8_t *p = NULL;
    size_t n = 0;
    
    // update_time
    if (hex_to_bytes("00000000001234560000000000abcdef01", &p, &n) == 0) {
      lc_update_time ut;
      if (lc_parse_update_time(p, n, &ut) != LC_OK) {
        fprintf(stderr, "lc_parse_update_time failed\n");
        failures++;
      } else if (ut.game_time != 0x123456 || ut.day_time != 0xabcdef || ut.tick_day_time != 1) {
        fprintf(stderr, "lc_parse_update_time wrong values: %lld %lld %d\n", (long long)ut.game_time, (long long)ut.day_time, ut.tick_day_time);
        failures++;
      }
      free(p);
    }


    // game_event
    if (hex_to_bytes("0340a00000", &p, &n) == 0) {
      lc_game_event ge;
      if (lc_parse_game_event(p, n, &ge) != LC_OK) {
        fprintf(stderr, "lc_parse_game_event failed\n");
        failures++;
      } else if (ge.event != 3 || ge.value != 5.0f) {
        fprintf(stderr, "lc_parse_game_event wrong values: %d %f\n", ge.event, ge.value);
        failures++;
      }
      free(p);
    }

    // set_ticking_state
    if (hex_to_bytes("41a0000000", &p, &n) == 0) {
      lc_set_ticking_state ts;
      if (lc_parse_set_ticking_state(p, n, &ts) != LC_OK) {
        fprintf(stderr, "lc_parse_set_ticking_state failed\n");
        failures++;
      } else if (ts.tick_rate != 20.0f || ts.is_frozen != 0) {
        fprintf(stderr, "lc_parse_set_ticking_state wrong values: %f %d\n", ts.tick_rate, ts.is_frozen);
        failures++;
      }
      free(p);
    }

    // update_health
    if (hex_to_bytes("41a000001440a00000", &p, &n) == 0) {
      lc_update_health uh;
      if (lc_parse_update_health(p, n, &uh) != LC_OK) {
        fprintf(stderr, "lc_parse_update_health failed\n");
        failures++;
      } else if (uh.health != 20.0f || uh.food != 20 || uh.saturation != 5.0f) {
        fprintf(stderr, "lc_parse_update_health wrong values: %f %d %f\n", uh.health, uh.food, uh.saturation);
        failures++;
      }
      free(p);
    }

    // update_view_position
    if (hex_to_bytes("0b16", &p, &n) == 0) {
      lc_update_view_position vp;
      if (lc_parse_update_view_position(p, n, &vp) != LC_OK) {
        fprintf(stderr, "lc_parse_update_view_position failed\n");
        failures++;
      } else if (vp.chunk_x != 11 || vp.chunk_z != 22) {
        fprintf(stderr, "lc_parse_update_view_position wrong values: %d %d\n", vp.chunk_x, vp.chunk_z);
        failures++;
      }
      free(p);
    }
  }

  return failures ? 1 : 0;
}


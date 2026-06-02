#include "decode_wire.h"

#include "libchunk.h"
#include "play_stream.h"

#include <string.h>
/* Good for: Return whether decode_wire knows this sniffer packet name.
 * Callers: decode_raw_dir.c, decode_wire.c (same file).
 */

int lc_packet_name_supported(const char *name) {
  if (lc_play_stream_packet_supported(name)) return 1;
  static const char *names[] = {
      "map_chunk",
      "unload_chunk",
      "update_light",
      "block_change",
      "tile_entity_data",
      "multi_block_change",
      "spawn_entity",
      "entity_metadata",
      "entity_equipment",
      "entity_destroy",
      "set_passengers",
      "rel_entity_move",
      "entity_move_look",
      "sync_entity_position",
      "entity_velocity",
      "entity_head_rotation",
      "entity_update_attributes",
      "position",
      "c2s_position",
      "c2s_position_look",
      "c2s_look",
      "c2s_flying",
      "c2s_teleport_confirm",
      "respawn",
      "initialize_world_border",
      "registry_data",
      "custom_payload",
      "feature_flags",
      "select_known_packs",
      "finish_configuration",
      "bundle_delimiter",
      "step_tick",
      "success",
      NULL,
  };
  for (size_t i = 0; names[i]; i++) {
    if (strcmp(name, names[i]) == 0) return 1;
  }
  return 0;
}

int lc_decode_wire_to_string(const char *name, const uint8_t *wire, size_t wire_len, char *out,
                             size_t out_sz) {
  if (!name || !wire || !out || out_sz == 0) return -1;
  if (!lc_packet_name_supported(name)) return 0;

  const uint8_t *after_id = wire;
  size_t after_len = wire_len;
  if (lc_skip_packet_id(wire, wire_len, &after_id, &after_len) != LC_OK) return -1;

  lc_status st = LC_ERR_INVALID;

  if (strcmp(name, "map_chunk") == 0) {
    lc_map_chunk p;
    memset(&p, 0, sizeof p);
    st = lc_parse_map_chunk(after_id, after_len, &p);
    if (st == LC_OK) {
      lc_map_chunk_to_string(&p, out, out_sz);
      lc_map_chunk_free(&p);
    }
  } else if (strcmp(name, "update_light") == 0) {
    lc_update_light p;
    memset(&p, 0, sizeof p);
    st = lc_parse_update_light(after_id, after_len, &p);
    if (st == LC_OK) {
      lc_update_light_to_string(&p, out, out_sz);
      lc_update_light_free(&p);
    }
  } else if (strcmp(name, "block_change") == 0) {
    lc_block_change p;
    st = lc_parse_block_change(after_id, after_len, &p);
    if (st == LC_OK) lc_block_change_to_string(&p, out, out_sz);
  } else if (strcmp(name, "unload_chunk") == 0) {
    lc_unload_chunk p;
    st = lc_parse_unload_chunk(after_id, after_len, &p);
    if (st == LC_OK) lc_unload_chunk_to_string(&p, out, out_sz);
  } else if (strcmp(name, "tile_entity_data") == 0) {
    lc_tile_entity_data p;
    memset(&p, 0, sizeof p);
    st = lc_parse_tile_entity_data(after_id, after_len, &p);
    if (st == LC_OK) {
      lc_tile_entity_data_to_string(&p, out, out_sz);
      lc_tile_entity_data_free(&p);
    }
  } else if (strcmp(name, "multi_block_change") == 0) {
    lc_multi_block_change p;
    memset(&p, 0, sizeof p);
    st = lc_parse_multi_block_change(after_id, after_len, &p);
    if (st == LC_OK) {
      lc_multi_block_change_to_string(&p, out, out_sz);
      lc_multi_block_change_free(&p);
    }
  } else if (strcmp(name, "spawn_entity") == 0) {
    lc_spawn_entity p;
    st = lc_parse_spawn_entity(after_id, after_len, &p);
    if (st == LC_OK) lc_spawn_entity_to_string(&p, out, out_sz);
  } else if (strcmp(name, "entity_metadata") == 0) {
    lc_entity_metadata p;
    memset(&p, 0, sizeof p);
    st = lc_parse_entity_metadata(after_id, after_len, &p);
    if (st == LC_OK) {
      lc_entity_metadata_to_string(&p, out, out_sz);
      lc_entity_metadata_free(&p);
    }
  } else if (strcmp(name, "entity_equipment") == 0) {
    lc_entity_equipment p;
    memset(&p, 0, sizeof p);
    st = lc_parse_entity_equipment(after_id, after_len, &p);
    if (st == LC_OK) {
      lc_entity_equipment_to_string(&p, out, out_sz);
      lc_entity_equipment_free(&p);
    }
  } else if (strcmp(name, "entity_destroy") == 0) {
    lc_entity_destroy p;
    memset(&p, 0, sizeof p);
    st = lc_parse_entity_destroy(after_id, after_len, &p);
    if (st == LC_OK) {
      lc_entity_destroy_to_string(&p, out, out_sz);
      lc_entity_destroy_free(&p);
    }
  } else if (strcmp(name, "set_passengers") == 0) {
    lc_set_passengers p;
    memset(&p, 0, sizeof p);
    st = lc_parse_set_passengers(after_id, after_len, &p);
    if (st == LC_OK) {
      lc_set_passengers_to_string(&p, out, out_sz);
      lc_set_passengers_free(&p);
    }
  } else if (strcmp(name, "rel_entity_move") == 0) {
    lc_rel_entity_move p;
    st = lc_parse_rel_entity_move(after_id, after_len, &p);
    if (st == LC_OK) lc_rel_entity_move_to_string(&p, out, out_sz);
  } else if (strcmp(name, "entity_move_look") == 0) {
    lc_entity_move_look p;
    st = lc_parse_entity_move_look(after_id, after_len, &p);
    if (st == LC_OK) lc_entity_move_look_to_string(&p, out, out_sz);
  } else if (strcmp(name, "sync_entity_position") == 0) {
    lc_sync_entity_position p;
    st = lc_parse_sync_entity_position(after_id, after_len, &p);
    if (st == LC_OK) lc_sync_entity_position_to_string(&p, out, out_sz);
  } else if (strcmp(name, "entity_velocity") == 0) {
    lc_entity_velocity p;
    st = lc_parse_entity_velocity(after_id, after_len, &p);
    if (st == LC_OK) lc_entity_velocity_to_string(&p, out, out_sz);
  } else if (strcmp(name, "entity_head_rotation") == 0) {
    lc_entity_head_rotation p;
    st = lc_parse_entity_head_rotation(after_id, after_len, &p);
    if (st == LC_OK) lc_entity_head_rotation_to_string(&p, out, out_sz);
  } else if (strcmp(name, "entity_update_attributes") == 0) {
    lc_entity_update_attributes p;
    memset(&p, 0, sizeof p);
    st = lc_parse_entity_update_attributes(after_id, after_len, &p);
    if (st == LC_OK) {
      lc_entity_update_attributes_to_string(&p, out, out_sz);
      lc_entity_update_attributes_free(&p);
    }
  } else if (strcmp(name, "position") == 0) {
    lc_position p;
    st = lc_parse_position(after_id, after_len, &p);
    if (st == LC_OK) lc_position_to_string(&p, out, out_sz);
  } else if (strcmp(name, "c2s_position") == 0) {
    lc_c2s_position p;
    st = lc_parse_c2s_position(after_id, after_len, &p);
    if (st == LC_OK) lc_c2s_position_to_string(&p, out, out_sz);
  } else if (strcmp(name, "c2s_position_look") == 0) {
    lc_c2s_position_look p;
    st = lc_parse_c2s_position_look(after_id, after_len, &p);
    if (st == LC_OK) lc_c2s_position_look_to_string(&p, out, out_sz);
  } else if (strcmp(name, "c2s_look") == 0) {
    lc_c2s_look p;
    st = lc_parse_c2s_look(after_id, after_len, &p);
    if (st == LC_OK) lc_c2s_look_to_string(&p, out, out_sz);
  } else if (strcmp(name, "c2s_flying") == 0) {
    lc_c2s_flying p;
    st = lc_parse_c2s_flying(after_id, after_len, &p);
    if (st == LC_OK) lc_c2s_flying_to_string(&p, out, out_sz);
  } else if (strcmp(name, "c2s_teleport_confirm") == 0) {
    lc_c2s_teleport_confirm p;
    st = lc_parse_c2s_teleport_confirm(after_id, after_len, &p);
    if (st == LC_OK) lc_c2s_teleport_confirm_to_string(&p, out, out_sz);
  } else if (strcmp(name, "respawn") == 0) {
    lc_respawn p;
    memset(&p, 0, sizeof p);
    st = lc_parse_respawn(after_id, after_len, &p);
    if (st == LC_OK) {
      lc_respawn_to_string(&p, out, out_sz);
      lc_respawn_free(&p);
    }
  } else if (strcmp(name, "initialize_world_border") == 0) {
    lc_initialize_world_border p;
    st = lc_parse_initialize_world_border(after_id, after_len, &p);
    if (st == LC_OK) lc_initialize_world_border_to_string(&p, out, out_sz);
  } else if (strcmp(name, "registry_data") == 0) {
    lc_registry_data p;
    memset(&p, 0, sizeof p);
    st = lc_parse_registry_data(after_id, after_len, &p);
    if (st == LC_OK) {
      lc_registry_data_to_string(&p, out, out_sz);
      lc_registry_data_free(&p);
    }
  } else if (strcmp(name, "custom_payload") == 0) {
    lc_custom_payload p;
    memset(&p, 0, sizeof p);
    st = lc_parse_custom_payload(after_id, after_len, &p);
    if (st == LC_OK) {
      lc_custom_payload_to_string(&p, out, out_sz);
      lc_custom_payload_free(&p);
    }
  } else if (strcmp(name, "feature_flags") == 0) {
    lc_feature_flags p;
    memset(&p, 0, sizeof p);
    st = lc_parse_feature_flags(after_id, after_len, &p);
    if (st == LC_OK) {
      lc_feature_flags_to_string(&p, out, out_sz);
      lc_feature_flags_free(&p);
    }
  } else if (strcmp(name, "select_known_packs") == 0) {
    lc_select_known_packs p;
    memset(&p, 0, sizeof p);
    st = lc_parse_select_known_packs(after_id, after_len, &p);
    if (st == LC_OK) {
      lc_select_known_packs_to_string(&p, out, out_sz);
      lc_select_known_packs_free(&p);
    }
  } else if (strcmp(name, "finish_configuration") == 0) {
    st = lc_parse_finish_configuration(after_id, after_len);
    if (st == LC_OK) lc_finish_configuration_to_string(out, out_sz);
  } else if (strcmp(name, "bundle_delimiter") == 0) {
    st = lc_parse_bundle_delimiter(after_id, after_len);
    if (st == LC_OK) lc_bundle_delimiter_to_string(out, out_sz);
  } else if (strcmp(name, "step_tick") == 0) {
    lc_step_tick p;
    memset(&p, 0, sizeof p);
    st = lc_parse_step_tick(after_id, after_len, &p);
    if (st == LC_OK) lc_step_tick_to_string(&p, out, out_sz);
  } else if (strcmp(name, "success") == 0) {
    lc_login_success p;
    memset(&p, 0, sizeof p);
    st = lc_parse_success(after_id, after_len, &p);
    if (st == LC_OK) {
      lc_success_to_string(&p, out, out_sz);
      lc_login_success_free(&p);
    }
  } else if (lc_play_stream_packet_supported(name)) {
    return lc_decode_play_stream_to_string(name, after_id, after_len, out, out_sz);
  }

  return st == LC_OK ? 1 : -1;
}

int lc_decode_wire_map_chunk_json(const char *name, const char *file_basename,
                                  const uint8_t *wire, size_t wire_len, FILE *out) {
  if (!name || !wire || !out) return -1;
  if (strcmp(name, "map_chunk") != 0) return 0;

  const uint8_t *after_id = wire;
  size_t after_len = wire_len;
  if (lc_skip_packet_id(wire, wire_len, &after_id, &after_len) != LC_OK) return -1;

  lc_map_chunk p;
  memset(&p, 0, sizeof p);
  lc_status st = lc_parse_map_chunk(after_id, after_len, &p);
  if (st != LC_OK) return -1;
  int rc = lc_map_chunk_dump_json(out, file_basename, wire, wire_len, &p);
  lc_map_chunk_free(&p);
  return rc == 0 ? 1 : -1;
}

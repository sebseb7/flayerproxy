#include "decode_wire.h"

#include "libchunk.h"
#include "play_stream.h"

#include <string.h>

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
      "sound_effect",
      "entity_sound_effect",
      "entity_update_attributes",
      "position",
      "c2s_position",
      "c2s_position_look",
      "c2s_look",
      "c2s_flying",
      "c2s_teleport_confirm",
      "c2s_block_dig",
      "c2s_player_input",
      "c2s_arm_animation",
      "c2s_container_close",
      "c2s_recipe_book_seen_recipe",
      "respawn",
      "world_event",
      "block_action",
      "explosion",
      "c2s_container_click",
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

int lc_decode_payload_to_string(const char *name, const uint8_t *payload, size_t payload_len,
                                char *out, size_t out_sz) {
  if (!name || !payload || !out || out_sz == 0) return -1;
  if (!lc_packet_name_supported(name)) return 0;

  lc_status st = LC_ERR_INVALID;

  if (strcmp(name, "map_chunk") == 0) {
    lc_map_chunk p;
    memset(&p, 0, sizeof p);
    st = lc_parse_map_chunk(payload, payload_len, &p);
    if (st == LC_OK) {
      lc_map_chunk_to_string(&p, out, out_sz);
      lc_map_chunk_free(&p);
    }
  } else if (strcmp(name, "update_light") == 0) {
    lc_update_light p;
    memset(&p, 0, sizeof p);
    st = lc_parse_update_light(payload, payload_len, &p);
    if (st == LC_OK) {
      lc_update_light_to_string(&p, out, out_sz);
      lc_update_light_free(&p);
    }
  } else if (strcmp(name, "block_change") == 0) {
    lc_block_change p;
    st = lc_parse_block_change(payload, payload_len, &p);
    if (st == LC_OK) lc_block_change_to_string(&p, out, out_sz);
  } else if (strcmp(name, "unload_chunk") == 0) {
    lc_unload_chunk p;
    st = lc_parse_unload_chunk(payload, payload_len, &p);
    if (st == LC_OK) lc_unload_chunk_to_string(&p, out, out_sz);
  } else if (strcmp(name, "tile_entity_data") == 0) {
    lc_tile_entity_data p;
    memset(&p, 0, sizeof p);
    st = lc_parse_tile_entity_data(payload, payload_len, &p);
    if (st == LC_OK) {
      lc_tile_entity_data_to_string(&p, out, out_sz);
      lc_tile_entity_data_free(&p);
    }
  } else if (strcmp(name, "multi_block_change") == 0) {
    lc_multi_block_change p;
    memset(&p, 0, sizeof p);
    st = lc_parse_multi_block_change(payload, payload_len, &p);
    if (st == LC_OK) {
      lc_multi_block_change_to_string(&p, out, out_sz);
      lc_multi_block_change_free(&p);
    }
  } else if (strcmp(name, "spawn_entity") == 0) {
    lc_spawn_entity p;
    st = lc_parse_spawn_entity(payload, payload_len, &p);
    if (st == LC_OK) lc_spawn_entity_to_string(&p, out, out_sz);
  } else if (strcmp(name, "entity_metadata") == 0) {
    lc_entity_metadata p;
    memset(&p, 0, sizeof p);
    st = lc_parse_entity_metadata(payload, payload_len, &p);
    if (st == LC_OK) {
      lc_entity_metadata_to_string(&p, out, out_sz);
      lc_entity_metadata_free(&p);
    }
  } else if (strcmp(name, "entity_equipment") == 0) {
    lc_entity_equipment p;
    memset(&p, 0, sizeof p);
    st = lc_parse_entity_equipment(payload, payload_len, &p);
    if (st == LC_OK) {
      lc_entity_equipment_to_string(&p, out, out_sz);
      lc_entity_equipment_free(&p);
    }
  } else if (strcmp(name, "entity_destroy") == 0) {
    lc_entity_destroy p;
    memset(&p, 0, sizeof p);
    st = lc_parse_entity_destroy(payload, payload_len, &p);
    if (st == LC_OK) {
      lc_entity_destroy_to_string(&p, out, out_sz);
      lc_entity_destroy_free(&p);
    }
  } else if (strcmp(name, "set_passengers") == 0) {
    lc_set_passengers p;
    memset(&p, 0, sizeof p);
    st = lc_parse_set_passengers(payload, payload_len, &p);
    if (st == LC_OK) {
      lc_set_passengers_to_string(&p, out, out_sz);
      lc_set_passengers_free(&p);
    }
  } else if (strcmp(name, "rel_entity_move") == 0) {
    lc_rel_entity_move p;
    st = lc_parse_rel_entity_move(payload, payload_len, &p);
    if (st == LC_OK) lc_rel_entity_move_to_string(&p, out, out_sz);
  } else if (strcmp(name, "entity_move_look") == 0) {
    lc_entity_move_look p;
    st = lc_parse_entity_move_look(payload, payload_len, &p);
    if (st == LC_OK) lc_entity_move_look_to_string(&p, out, out_sz);
  } else if (strcmp(name, "sync_entity_position") == 0) {
    lc_sync_entity_position p;
    st = lc_parse_sync_entity_position(payload, payload_len, &p);
    if (st == LC_OK) lc_sync_entity_position_to_string(&p, out, out_sz);
  } else if (strcmp(name, "entity_velocity") == 0) {
    lc_entity_velocity p;
    st = lc_parse_entity_velocity(payload, payload_len, &p);
    if (st == LC_OK) lc_entity_velocity_to_string(&p, out, out_sz);
  } else if (strcmp(name, "entity_head_rotation") == 0) {
    lc_entity_head_rotation p;
    st = lc_parse_entity_head_rotation(payload, payload_len, &p);
    if (st == LC_OK) lc_entity_head_rotation_to_string(&p, out, out_sz);
  } else if (strcmp(name, "sound_effect") == 0) {
    lc_sound_effect p;
    memset(&p, 0, sizeof p);
    st = lc_parse_sound_effect(payload, payload_len, &p);
    if (st == LC_OK) {
      lc_sound_effect_to_string(&p, out, out_sz);
      lc_sound_effect_free(&p);
    }
  } else if (strcmp(name, "entity_sound_effect") == 0) {
    lc_entity_sound_effect p;
    memset(&p, 0, sizeof p);
    st = lc_parse_entity_sound_effect(payload, payload_len, &p);
    if (st == LC_OK) {
      lc_entity_sound_effect_to_string(&p, out, out_sz);
      lc_entity_sound_effect_free(&p);
    }
  } else if (strcmp(name, "entity_update_attributes") == 0) {
    lc_entity_update_attributes p;
    memset(&p, 0, sizeof p);
    st = lc_parse_entity_update_attributes(payload, payload_len, &p);
    if (st == LC_OK) {
      lc_entity_update_attributes_to_string(&p, out, out_sz);
      lc_entity_update_attributes_free(&p);
    }
  } else if (strcmp(name, "position") == 0) {
    lc_position p;
    st = lc_parse_position(payload, payload_len, &p);
    if (st == LC_OK) lc_position_to_string(&p, out, out_sz);
  } else if (strcmp(name, "c2s_position") == 0) {
    lc_c2s_position p;
    st = lc_parse_c2s_position(payload, payload_len, &p);
    if (st == LC_OK) lc_c2s_position_to_string(&p, out, out_sz);
  } else if (strcmp(name, "c2s_position_look") == 0) {
    lc_c2s_position_look p;
    st = lc_parse_c2s_position_look(payload, payload_len, &p);
    if (st == LC_OK) lc_c2s_position_look_to_string(&p, out, out_sz);
  } else if (strcmp(name, "c2s_look") == 0) {
    lc_c2s_look p;
    st = lc_parse_c2s_look(payload, payload_len, &p);
    if (st == LC_OK) lc_c2s_look_to_string(&p, out, out_sz);
  } else if (strcmp(name, "c2s_flying") == 0) {
    lc_c2s_flying p;
    st = lc_parse_c2s_flying(payload, payload_len, &p);
    if (st == LC_OK) lc_c2s_flying_to_string(&p, out, out_sz);
  } else if (strcmp(name, "c2s_teleport_confirm") == 0) {
    lc_c2s_teleport_confirm p;
    st = lc_parse_c2s_teleport_confirm(payload, payload_len, &p);
    if (st == LC_OK) lc_c2s_teleport_confirm_to_string(&p, out, out_sz);
  } else if (strcmp(name, "c2s_block_dig") == 0) {
    lc_c2s_block_dig p;
    st = lc_parse_c2s_block_dig(payload, payload_len, &p);
    if (st == LC_OK) lc_c2s_block_dig_to_string(&p, out, out_sz);
  } else if (strcmp(name, "c2s_player_input") == 0) {
    lc_c2s_player_input p;
    st = lc_parse_c2s_player_input(payload, payload_len, &p);
    if (st == LC_OK) lc_c2s_player_input_to_string(&p, out, out_sz);
  } else if (strcmp(name, "c2s_arm_animation") == 0) {
    lc_c2s_arm_animation p;
    st = lc_parse_c2s_arm_animation(payload, payload_len, &p);
    if (st == LC_OK) lc_c2s_arm_animation_to_string(&p, out, out_sz);
  } else if (strcmp(name, "c2s_container_close") == 0) {
    lc_c2s_container_close p;
    st = lc_parse_c2s_container_close(payload, payload_len, &p);
    if (st == LC_OK) lc_c2s_container_close_to_string(&p, out, out_sz);
  } else if (strcmp(name, "c2s_recipe_book_seen_recipe") == 0) {
    lc_c2s_recipe_book_seen_recipe p;
    st = lc_parse_c2s_recipe_book_seen_recipe(payload, payload_len, &p);
    if (st == LC_OK) lc_c2s_recipe_book_seen_recipe_to_string(&p, out, out_sz);
  } else if (strcmp(name, "respawn") == 0) {
    lc_respawn p;
    memset(&p, 0, sizeof p);
    st = lc_parse_respawn(payload, payload_len, &p);
    if (st == LC_OK) {
      lc_respawn_to_string(&p, out, out_sz);
      lc_respawn_free(&p);
    }
  } else if (strcmp(name, "world_event") == 0) {
    lc_world_event p;
    st = lc_parse_world_event(payload, payload_len, &p);
    if (st == LC_OK) {
      lc_world_event_to_string(&p, out, out_sz);
    }
  } else if (strcmp(name, "block_action") == 0) {
    lc_block_action p;
    st = lc_parse_block_action(payload, payload_len, &p);
    if (st == LC_OK) lc_block_action_to_string(&p, out, out_sz);
  } else if (strcmp(name, "explosion") == 0) {
    lc_explosion p;
    st = lc_parse_explosion(payload, payload_len, &p);
    if (st == LC_OK) lc_explosion_to_string(&p, out, out_sz);
  } else if (strcmp(name, "c2s_container_click") == 0) {
    lc_c2s_container_click p;
    st = lc_parse_c2s_container_click(payload, payload_len, &p);
    if (st == LC_OK) lc_c2s_container_click_to_string(&p, out, out_sz);
  } else if (strcmp(name, "initialize_world_border") == 0) {
    lc_initialize_world_border p;
    st = lc_parse_initialize_world_border(payload, payload_len, &p);
    if (st == LC_OK) lc_initialize_world_border_to_string(&p, out, out_sz);
  } else if (strcmp(name, "registry_data") == 0) {
    lc_registry_data p;
    memset(&p, 0, sizeof p);
    st = lc_parse_registry_data(payload, payload_len, &p);
    if (st == LC_OK) {
      lc_registry_data_to_string(&p, out, out_sz);
      lc_registry_data_free(&p);
    }
  } else if (strcmp(name, "custom_payload") == 0) {
    lc_custom_payload p;
    memset(&p, 0, sizeof p);
    st = lc_parse_custom_payload(payload, payload_len, &p);
    if (st == LC_OK) {
      lc_custom_payload_to_string(&p, out, out_sz);
      lc_custom_payload_free(&p);
    }
  } else if (strcmp(name, "feature_flags") == 0) {
    lc_feature_flags p;
    memset(&p, 0, sizeof p);
    st = lc_parse_feature_flags(payload, payload_len, &p);
    if (st == LC_OK) {
      lc_feature_flags_to_string(&p, out, out_sz);
      lc_feature_flags_free(&p);
    }
  } else if (strcmp(name, "select_known_packs") == 0) {
    lc_select_known_packs p;
    memset(&p, 0, sizeof p);
    st = lc_parse_select_known_packs(payload, payload_len, &p);
    if (st == LC_OK) {
      lc_select_known_packs_to_string(&p, out, out_sz);
      lc_select_known_packs_free(&p);
    }
  } else if (strcmp(name, "finish_configuration") == 0) {
    st = lc_parse_finish_configuration(payload, payload_len);
    if (st == LC_OK) lc_finish_configuration_to_string(out, out_sz);
  } else if (strcmp(name, "bundle_delimiter") == 0) {
    st = lc_parse_bundle_delimiter(payload, payload_len);
    if (st == LC_OK) lc_bundle_delimiter_to_string(out, out_sz);
  } else if (strcmp(name, "step_tick") == 0) {
    lc_step_tick p;
    memset(&p, 0, sizeof p);
    st = lc_parse_step_tick(payload, payload_len, &p);
    if (st == LC_OK) lc_step_tick_to_string(&p, out, out_sz);
  } else if (strcmp(name, "success") == 0) {
    lc_login_success p;
    memset(&p, 0, sizeof p);
    st = lc_parse_success(payload, payload_len, &p);
    if (st == LC_OK) {
      lc_success_to_string(&p, out, out_sz);
      lc_login_success_free(&p);
    }
  } else if (lc_play_stream_packet_supported(name)) {
    return lc_decode_play_stream_to_string(name, payload, payload_len, out, out_sz);
  }

  return st == LC_OK ? 1 : -1;
}

int lc_decode_wire_to_string(const char *name, const uint8_t *wire, size_t wire_len, char *out,
                             size_t out_sz) {
  if (!name || !wire || !out || out_sz == 0) return -1;

  const uint8_t *payload = wire;
  size_t payload_len = wire_len;
  if (lc_skip_packet_id(wire, wire_len, &payload, &payload_len) != LC_OK) return -1;

  return lc_decode_payload_to_string(name, payload, payload_len, out, out_sz);
}

int lc_decode_payload_map_chunk_json(const char *name, const char *file_basename,
                                     const uint8_t *payload, size_t payload_len, FILE *out) {
  if (!name || !payload || !out) return -1;
  if (strcmp(name, "map_chunk") != 0) return 0;

  lc_map_chunk p;
  memset(&p, 0, sizeof p);
  lc_status st = lc_parse_map_chunk(payload, payload_len, &p);
  if (st != LC_OK) return -1;
  int rc = lc_map_chunk_dump_json(out, file_basename, payload, payload_len, &p);
  lc_map_chunk_free(&p);
  return rc == 0 ? 1 : -1;
}

int lc_decode_wire_map_chunk_json(const char *name, const char *file_basename,
                                  const uint8_t *wire, size_t wire_len, FILE *out) {
  if (!name || !wire || !out) return -1;
  if (strcmp(name, "map_chunk") != 0) return 0;

  const uint8_t *payload = wire;
  size_t payload_len = wire_len;
  if (lc_skip_packet_id(wire, wire_len, &payload, &payload_len) != LC_OK) return -1;

  return lc_decode_payload_map_chunk_json(name, file_basename, payload, payload_len, out);
}

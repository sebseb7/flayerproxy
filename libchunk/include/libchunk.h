/**
 * libchunk — Minecraft Java Edition play/configuration packet parsers (1.21.x wire).
 *
 * Input buffers are packet *payload* bytes (no leading packet-id varint).
 * Use lc_skip_packet_id() when your capture includes the varint packet id prefix.
 */
#ifndef LIBCHUNK_H
#define LIBCHUNK_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LC_VERSION "0.1.0"

typedef enum lc_status {
  LC_OK = 0,
  LC_ERR_TRUNCATED = -1,
  LC_ERR_INVALID = -2,
  LC_ERR_OOM = -3,
} lc_status;

/* --- shared types --- */

typedef struct lc_vec3 {
  double x, y, z;
} lc_vec3;

typedef struct lc_block_pos {
  int32_t x, y, z;
} lc_block_pos;

typedef struct lc_chunk_section_pos {
  int32_t x, z, y; /* section chunk coords from multi_block_change */
} lc_chunk_section_pos;

typedef struct lc_uuid {
  uint8_t bytes[16];
} lc_uuid;

typedef struct lc_byte_buf {
  uint8_t *data;
  size_t len;
} lc_byte_buf;

typedef struct lc_i64_arr {
  int64_t *values;
  size_t count;
} lc_i64_arr;

typedef struct lc_u8_arr {
  uint8_t *values;
  size_t count;
} lc_u8_arr;

typedef struct lc_u8_grid {
  uint8_t **rows;
  size_t row_count;
  size_t *row_lens;
} lc_u8_grid;

typedef struct lc_heightmap {
  const char *type_name;
  int type_id;
  lc_i64_arr data;
} lc_heightmap;

typedef struct lc_heightmap_arr {
  lc_heightmap *items;
  size_t count;
} lc_heightmap_arr;

typedef struct lc_block_entity {
  uint8_t x, z; /* 4-bit chunk-local */
  int16_t y;
  int32_t type;
  lc_byte_buf nbt; /* raw anonymous NBT bytes, may be empty */
} lc_block_entity;

typedef struct lc_block_entity_arr {
  lc_block_entity *items;
  size_t count;
} lc_block_entity_arr;

typedef enum lc_meta_value_kind {
  LC_META_NONE = 0,
  LC_META_BYTE,
  LC_META_INT,
  LC_META_LONG,
  LC_META_FLOAT,
  LC_META_DOUBLE,
  LC_META_STRING,
  LC_META_BOOL,
  LC_META_VARINT,
  LC_META_RAW, /* complex / unparsed — raw value bytes */
} lc_meta_value_kind;

typedef struct lc_metadata_entry {
  uint8_t key;
  int type_id;
  const char *type_name;
  lc_meta_value_kind kind;
  union {
    int8_t i8;
    int32_t i32;
    int64_t i64;
    float f32;
    double f64;
    uint8_t boolean;
    char *string;
    lc_byte_buf raw;
  } v;
} lc_metadata_entry;

typedef struct lc_metadata_arr {
  lc_metadata_entry *items;
  size_t count;
} lc_metadata_arr;

typedef struct lc_equipment {
  int8_t slot;
  int32_t item_count;
  int32_t item_id; /* -1 if empty stack */
  lc_byte_buf item_extra; /* remainder of Slot if non-empty */
} lc_equipment;

typedef struct lc_equipment_arr {
  lc_equipment *items;
  size_t count;
} lc_equipment_arr;

typedef struct lc_registry_entry {
  char *key;
  lc_byte_buf nbt; /* absent => len 0 */
} lc_registry_entry;

typedef struct lc_registry_entry_arr {
  lc_registry_entry *items;
  size_t count;
} lc_registry_entry_arr;

typedef struct lc_spawn_info {
  int32_t dimension;
  char *name;
  int64_t hashed_seed;
  int8_t gamemode; /* 0=survival .. */
  uint8_t previous_gamemode;
  uint8_t is_debug;
  uint8_t is_flat;
  uint8_t has_death;
  char *death_dimension_name;
  lc_block_pos death_pos;
  int32_t portal_cooldown;
  int32_t sea_level;
} lc_spawn_info;

/* --- packet structs --- */

typedef struct lc_map_chunk {
  int32_t x, z;
  lc_heightmap_arr heightmaps;
  lc_byte_buf chunk_data;
  lc_block_entity_arr block_entities;
  lc_i64_arr sky_light_mask;
  lc_i64_arr block_light_mask;
  lc_i64_arr empty_sky_light_mask;
  lc_i64_arr empty_block_light_mask;
  lc_u8_grid sky_light;
  lc_u8_grid block_light;
} lc_map_chunk;

typedef struct lc_update_light {
  int32_t chunk_x, chunk_z;
  lc_i64_arr sky_light_mask;
  lc_i64_arr block_light_mask;
  lc_i64_arr empty_sky_light_mask;
  lc_i64_arr empty_block_light_mask;
  lc_u8_grid sky_light;
  lc_u8_grid block_light;
} lc_update_light;

typedef struct lc_block_change {
  lc_block_pos location;
  int32_t type;
} lc_block_change;

typedef struct lc_unload_chunk {
  int32_t x;
  int32_t z;
} lc_unload_chunk;

typedef struct lc_tile_entity_data {
  lc_block_pos location;
  int32_t action;
  uint8_t nbt_present;
  lc_byte_buf nbt;
} lc_tile_entity_data;

typedef struct lc_multi_block_change {
  lc_chunk_section_pos chunk_coordinates;
  int32_t *records;
  size_t record_count;
} lc_multi_block_change;

typedef struct lc_spawn_entity {
  int32_t entity_id;
  lc_uuid object_uuid;
  int32_t type;
  double x, y, z;
  lc_vec3 velocity;
  int8_t pitch, yaw, head_pitch;
  int32_t object_data;
} lc_spawn_entity;

typedef struct lc_entity_metadata {
  int32_t entity_id;
  lc_metadata_arr metadata;
} lc_entity_metadata;

typedef struct lc_entity_equipment {
  int32_t entity_id;
  lc_equipment_arr equipments;
} lc_entity_equipment;

typedef struct lc_entity_destroy {
  int32_t *entity_ids;
  size_t count;
} lc_entity_destroy;

typedef struct lc_set_passengers {
  int32_t entity_id;
  int32_t *passengers;
  size_t passenger_count;
} lc_set_passengers;

typedef struct lc_rel_entity_move {
  int32_t entity_id;
  int16_t dx, dy, dz;
  uint8_t on_ground;
} lc_rel_entity_move;

typedef struct lc_entity_move_look {
  int32_t entity_id;
  int16_t dx, dy, dz;
  int8_t yaw, pitch;
  uint8_t on_ground;
} lc_entity_move_look;

typedef struct lc_sync_entity_position {
  int32_t entity_id;
  double x, y, z;
  double dx, dy, dz;
  float yaw, pitch;
  uint8_t on_ground;
} lc_sync_entity_position;

typedef struct lc_entity_velocity {
  int32_t entity_id;
  lc_vec3 velocity;
} lc_entity_velocity;

typedef struct lc_sound_event_ref {
  char *id;
  uint8_t has_fixed_range;
  float fixed_range;
} lc_sound_event_ref;

typedef struct lc_sound_effect {
  lc_sound_event_ref sound;
  int32_t source;
  int32_t x, y, z;
  float volume;
  float pitch;
  int64_t seed;
} lc_sound_effect;

typedef struct lc_entity_sound_effect {
  lc_sound_event_ref sound;
  int32_t source;
  int32_t entity_id;
  float volume;
  float pitch;
  int64_t seed;
} lc_entity_sound_effect;

typedef struct lc_entity_head_rotation {
  int32_t entity_id;
  int8_t head_yaw;
} lc_entity_head_rotation;

typedef struct lc_entity_look {
  int32_t entity_id;
  int8_t yaw, pitch;
  uint8_t on_ground;
} lc_entity_look;

typedef struct lc_entity_teleport {
  int32_t entity_id;
  double x, y, z;
  int8_t yaw, pitch;
  uint8_t on_ground;
} lc_entity_teleport;

typedef struct lc_entity_attribute_modifier {
  char *uuid;
  double amount;
  int8_t operation;
} lc_entity_attribute_modifier;

typedef struct lc_entity_attribute_property {
  int32_t key;
  double value;
  lc_entity_attribute_modifier *modifiers;
  size_t modifier_count;
} lc_entity_attribute_property;

typedef struct lc_entity_update_attributes {
  int32_t entity_id;
  lc_entity_attribute_property *properties;
  size_t property_count;
} lc_entity_update_attributes;

typedef struct lc_entity_status {
  int32_t entity_id;
  int8_t status;
} lc_entity_status;

typedef struct lc_entity_effect {
  int32_t entity_id;
  int32_t effect_id;
  int32_t amplifier;
  int32_t duration;
  uint8_t flags;
} lc_entity_effect;

typedef struct lc_remove_entity_effect {
  int32_t entity_id;
  int32_t effect_id;
} lc_remove_entity_effect;

typedef struct lc_attach_entity {
  int32_t attached_id;
  int32_t holding_id;
} lc_attach_entity;


typedef struct lc_position {
  int32_t teleport_id;
  double x, y, z;
  double dx, dy, dz;
  float yaw, pitch;
  uint32_t flags;
} lc_position;

/** Serverbound move player (client → server). */
typedef struct lc_c2s_move_flags {
  uint8_t raw;
  int on_ground;
  int horizontal_collision;
} lc_c2s_move_flags;

typedef struct lc_c2s_position {
  double x, y, z;
  lc_c2s_move_flags flags;
} lc_c2s_position;

typedef struct lc_c2s_position_look {
  double x, y, z;
  float yaw, pitch;
  lc_c2s_move_flags flags;
} lc_c2s_position_look;

typedef struct lc_c2s_look {
  float yaw, pitch;
  lc_c2s_move_flags flags;
} lc_c2s_look;

typedef struct lc_c2s_flying {
  lc_c2s_move_flags flags;
} lc_c2s_flying;

typedef struct lc_c2s_teleport_confirm {
  int32_t teleport_id;
} lc_c2s_teleport_confirm;

typedef struct lc_c2s_block_dig {
  int32_t status;
  lc_block_pos location;
  int8_t face;
  int32_t sequence;
} lc_c2s_block_dig;

typedef struct lc_c2s_player_input {
  uint8_t raw;
  int forward;
  int backward;
  int left;
  int right;
  int jump;
  int shift;
  int sprint;
} lc_c2s_player_input;

typedef struct lc_c2s_arm_animation {
  int32_t hand;
} lc_c2s_arm_animation;

typedef struct lc_c2s_container_close {
  int32_t container_id;
} lc_c2s_container_close;

typedef struct lc_c2s_recipe_book_seen_recipe {
  int32_t recipe_id;
} lc_c2s_recipe_book_seen_recipe;


typedef struct lc_respawn {
  lc_spawn_info world_state;
  uint8_t copy_metadata;
} lc_respawn;

typedef struct lc_initialize_world_border {
  double x, z;
  double old_diameter, new_diameter;
  int32_t speed;
  int32_t portal_teleport_boundary;
  int32_t warning_blocks;
  int32_t warning_time;
} lc_initialize_world_border;

typedef struct lc_custom_payload {
  char *channel;
  lc_byte_buf data;
} lc_custom_payload;

typedef struct lc_feature_flags {
  char **flags;
  size_t count;
} lc_feature_flags;

typedef struct lc_known_pack {
  char *pack_namespace;
  char *id;
  char *version;
} lc_known_pack;

typedef struct lc_select_known_packs {
  lc_known_pack *packs;
  size_t count;
} lc_select_known_packs;

typedef struct lc_step_tick {
  uint8_t skip_tick;
} lc_step_tick;

typedef struct lc_login_property {
  char *name;
  char *value;
  char *signature;
} lc_login_property;

typedef struct lc_login_success {
  lc_uuid uuid;
  char *username;
  lc_login_property *properties;
  size_t property_count;
} lc_login_success;

typedef struct lc_registry_data {
  char *id;
  lc_registry_entry_arr entries;
} lc_registry_data;

typedef struct lc_tag_group_entry {
  char *name;
  int32_t *ids;
  size_t id_count;
} lc_tag_group_entry;

typedef struct lc_tag_group {
  char *registry_id;
  lc_tag_group_entry *tags;
  size_t tag_count;
} lc_tag_group;

typedef struct lc_update_tags {
  lc_tag_group *groups;
  size_t group_count;
} lc_update_tags;

typedef struct lc_update_time {
  int64_t game_time;
  int64_t day_time;
  uint8_t tick_day_time;
} lc_update_time;

typedef struct lc_game_event {
  uint8_t event;
  float value;
} lc_game_event;

typedef struct lc_set_ticking_state {
  float tick_rate;
  uint8_t is_frozen;
} lc_set_ticking_state;

typedef struct lc_update_health {
  float health;
  int32_t food;
  float saturation;
} lc_update_health;

typedef struct lc_update_view_position {
  int32_t chunk_x;
  int32_t chunk_z;
} lc_update_view_position;

typedef struct lc_world_event {
  int32_t type;
  lc_block_pos location;
  int32_t data;
  uint8_t global;
} lc_world_event;



/* --- merged chunk column --- */

#define LC_BLOCK_VOLUME 4096
#define LC_BIOME_VOLUME 64
#define LC_CHUNK_DEFAULT_MIN_Y (-64)
#define LC_CHUNK_DEFAULT_WORLD_HEIGHT 384

typedef struct lc_chunk_section {
  int32_t section_y;
  int16_t solid_block_count;
  int32_t state_ids[LC_BLOCK_VOLUME];
  uint8_t has_biomes;
  int32_t biome_ids[LC_BIOME_VOLUME];
} lc_chunk_section;

typedef struct lc_chunk {
  int32_t x, z;
  int32_t min_y;
  int32_t world_height;
  lc_chunk_section *sections;
  size_t section_count;
  lc_heightmap_arr heightmaps;
  lc_block_entity_arr block_entities;
  lc_i64_arr sky_light_mask;
  lc_i64_arr block_light_mask;
  lc_i64_arr empty_sky_light_mask;
  lc_i64_arr empty_block_light_mask;
  lc_u8_grid sky_light;
  lc_u8_grid block_light;
} lc_chunk;

/* --- utilities --- */

/** Skip leading play/configuration packet-id varint; returns new pointer and length. */
lc_status lc_skip_packet_id(const uint8_t *data, size_t len, const uint8_t **payload, size_t *payload_len);

void lc_byte_buf_free(lc_byte_buf *b);
void lc_i64_arr_free(lc_i64_arr *a);
void lc_u8_grid_free(lc_u8_grid *g);
void lc_heightmap_arr_free(lc_heightmap_arr *a);
void lc_block_entity_arr_free(lc_block_entity_arr *a);
/** Known map_chunk block-entity type ids; NULL if unknown. */
const char *lc_block_entity_type_name(int32_t type);
void lc_metadata_arr_free(lc_metadata_arr *a);
void lc_equipment_arr_free(lc_equipment_arr *a);
void lc_registry_entry_arr_free(lc_registry_entry_arr *a);
void lc_spawn_info_free(lc_spawn_info *s);

void lc_map_chunk_free(lc_map_chunk *p);
void lc_update_light_free(lc_update_light *p);
void lc_multi_block_change_free(lc_multi_block_change *p);
void lc_entity_metadata_free(lc_entity_metadata *p);
void lc_sound_event_ref_free(lc_sound_event_ref *p);
void lc_sound_effect_free(lc_sound_effect *p);
void lc_entity_sound_effect_free(lc_entity_sound_effect *p);
void lc_entity_equipment_free(lc_entity_equipment *p);
void lc_entity_destroy_free(lc_entity_destroy *p);
void lc_set_passengers_free(lc_set_passengers *p);
void lc_registry_data_free(lc_registry_data *p);
void lc_update_tags_free(lc_update_tags *p);
void lc_tile_entity_data_free(lc_tile_entity_data *p);
void lc_entity_update_attributes_free(lc_entity_update_attributes *p);
void lc_respawn_free(lc_respawn *p);
void lc_custom_payload_free(lc_custom_payload *p);
void lc_feature_flags_free(lc_feature_flags *p);
void lc_select_known_packs_free(lc_select_known_packs *p);
void lc_login_success_free(lc_login_success *p);

void lc_chunk_init(lc_chunk *c);
void lc_chunk_free(lc_chunk *c);
lc_status lc_chunk_from_map_chunk(const lc_map_chunk *mc, lc_chunk *out);
lc_status lc_chunk_apply_update_light(lc_chunk *c, const lc_update_light *ul);
lc_status lc_chunk_apply_block_change(lc_chunk *c, const lc_block_change *bc);
lc_status lc_chunk_apply_multi_block_change(lc_chunk *c, const lc_multi_block_change *mbc);
/** Scan block columns and fill heightmaps (types 1,4,5; Java send order). Replaces c->heightmaps. */
lc_status lc_chunk_build_heightmaps(lc_chunk *c);
/** Export merged state as map_chunk fields (re-encodes chunk_data). `out` must be zero-initialized or cleared with lc_map_chunk_free() before reuse. */
lc_status lc_chunk_to_map_chunk(const lc_chunk *c, lc_map_chunk *out);
lc_status lc_chunk_serialize(const lc_chunk *c, lc_byte_buf *out);
lc_status lc_chunk_deserialize(const uint8_t *data, size_t len, lc_chunk *out);

/* --- parsers (payload only) --- */

lc_status lc_parse_map_chunk(const uint8_t *data, size_t len, lc_map_chunk *out);
/** Read chunk column x/z only (first 8 bytes of map_chunk payload). */
lc_status lc_peek_map_chunk_coords(const uint8_t *data, size_t len, int32_t *x, int32_t *z);
lc_status lc_parse_update_light(const uint8_t *data, size_t len, lc_update_light *out);
lc_status lc_parse_block_change(const uint8_t *data, size_t len, lc_block_change *out);
lc_status lc_parse_unload_chunk(const uint8_t *data, size_t len, lc_unload_chunk *out);
lc_status lc_parse_tile_entity_data(const uint8_t *data, size_t len, lc_tile_entity_data *out);
lc_status lc_parse_multi_block_change(const uint8_t *data, size_t len, lc_multi_block_change *out);
lc_status lc_parse_spawn_entity(const uint8_t *data, size_t len, lc_spawn_entity *out);
lc_status lc_parse_entity_metadata(const uint8_t *data, size_t len, lc_entity_metadata *out);
lc_status lc_parse_entity_equipment(const uint8_t *data, size_t len, lc_entity_equipment *out);
lc_status lc_parse_entity_destroy(const uint8_t *data, size_t len, lc_entity_destroy *out);
lc_status lc_parse_set_passengers(const uint8_t *data, size_t len, lc_set_passengers *out);
lc_status lc_parse_rel_entity_move(const uint8_t *data, size_t len, lc_rel_entity_move *out);
lc_status lc_parse_entity_move_look(const uint8_t *data, size_t len, lc_entity_move_look *out);
lc_status lc_parse_sync_entity_position(const uint8_t *data, size_t len, lc_sync_entity_position *out);
lc_status lc_parse_entity_velocity(const uint8_t *data, size_t len, lc_entity_velocity *out);
lc_status lc_parse_sound_effect(const uint8_t *data, size_t len, lc_sound_effect *out);
lc_status lc_parse_entity_sound_effect(const uint8_t *data, size_t len, lc_entity_sound_effect *out);
lc_status lc_parse_entity_head_rotation(const uint8_t *data, size_t len, lc_entity_head_rotation *out);
lc_status lc_parse_entity_look(const uint8_t *data, size_t len, lc_entity_look *out);
lc_status lc_parse_entity_teleport(const uint8_t *data, size_t len, lc_entity_teleport *out);
lc_status lc_parse_entity_update_attributes(const uint8_t *data, size_t len,
                                            lc_entity_update_attributes *out);
lc_status lc_parse_entity_status(const uint8_t *data, size_t len, lc_entity_status *out);
lc_status lc_parse_entity_effect(const uint8_t *data, size_t len, lc_entity_effect *out);
lc_status lc_parse_remove_entity_effect(const uint8_t *data, size_t len, lc_remove_entity_effect *out);
lc_status lc_parse_attach_entity(const uint8_t *data, size_t len, lc_attach_entity *out);

lc_status lc_parse_position(const uint8_t *data, size_t len, lc_position *out);
lc_status lc_parse_c2s_position(const uint8_t *data, size_t len, lc_c2s_position *out);
lc_status lc_parse_c2s_position_look(const uint8_t *data, size_t len, lc_c2s_position_look *out);
lc_status lc_parse_c2s_look(const uint8_t *data, size_t len, lc_c2s_look *out);
lc_status lc_parse_c2s_flying(const uint8_t *data, size_t len, lc_c2s_flying *out);
lc_status lc_parse_c2s_teleport_confirm(const uint8_t *data, size_t len, lc_c2s_teleport_confirm *out);
lc_status lc_parse_c2s_block_dig(const uint8_t *data, size_t len, lc_c2s_block_dig *out);
lc_status lc_parse_c2s_player_input(const uint8_t *data, size_t len, lc_c2s_player_input *out);
lc_status lc_parse_c2s_arm_animation(const uint8_t *data, size_t len, lc_c2s_arm_animation *out);
lc_status lc_parse_c2s_container_close(const uint8_t *data, size_t len, lc_c2s_container_close *out);
lc_status lc_parse_c2s_recipe_book_seen_recipe(const uint8_t *data, size_t len, lc_c2s_recipe_book_seen_recipe *out);

lc_status lc_parse_respawn(const uint8_t *data, size_t len, lc_respawn *out);
lc_status lc_parse_initialize_world_border(const uint8_t *data, size_t len, lc_initialize_world_border *out);
lc_status lc_parse_custom_payload(const uint8_t *data, size_t len, lc_custom_payload *out);
lc_status lc_parse_feature_flags(const uint8_t *data, size_t len, lc_feature_flags *out);
lc_status lc_parse_select_known_packs(const uint8_t *data, size_t len, lc_select_known_packs *out);
lc_status lc_parse_finish_configuration(const uint8_t *data, size_t len);
lc_status lc_parse_bundle_delimiter(const uint8_t *data, size_t len);
lc_status lc_parse_step_tick(const uint8_t *data, size_t len, lc_step_tick *out);
lc_status lc_parse_success(const uint8_t *data, size_t len, lc_login_success *out);
lc_status lc_parse_registry_data(const uint8_t *data, size_t len, lc_registry_data *out);
lc_status lc_parse_update_tags(const uint8_t *data, size_t len, lc_update_tags *out);
lc_status lc_parse_update_time(const uint8_t *data, size_t len, lc_update_time *out);
lc_status lc_parse_game_event(const uint8_t *data, size_t len, lc_game_event *out);
lc_status lc_parse_set_ticking_state(const uint8_t *data, size_t len, lc_set_ticking_state *out);
lc_status lc_parse_update_health(const uint8_t *data, size_t len, lc_update_health *out);
lc_status lc_parse_update_view_position(const uint8_t *data, size_t len, lc_update_view_position *out);
lc_status lc_parse_world_event(const uint8_t *data, size_t len, lc_world_event *out);

/**
 * Write human-readable debug summary into buf (NUL-terminated if buflen > 0).
 * Returns bytes that would have been written (excluding NUL), like snprintf.
 */
int lc_map_chunk_to_string(const lc_map_chunk *p, char *buf, size_t buflen);
/** Full JSON dump (all fields, decoded sections, base64 buffers). Returns 0 on success. */
int lc_map_chunk_dump_json(FILE *f, const char *file_basename, const uint8_t *wire, size_t wire_len,
                           const lc_map_chunk *mc);

/** Per-chunk top-down map PNG: 16×16 blocks at LC_MAP_CHUNK_PNG_SCALE px/block. */
#define LC_MAP_CHUNK_BLOCKS_PER_SIDE 16
#define LC_MAP_CHUNK_PNG_SCALE 2
#define LC_MAP_CHUNK_PNG_SIZE (LC_MAP_CHUNK_BLOCKS_PER_SIDE * LC_MAP_CHUNK_PNG_SCALE)
/** Mega-tiles cover 16×16 chunks (4096×4096 px). */
#define LC_MAP_TILE_CHUNKS_PER_SIDE 16
#define LC_MAP_TILE_PNG_SIZE (LC_MAP_CHUNK_PNG_SIZE * LC_MAP_TILE_CHUNKS_PER_SIDE)

/** LC_MAP_CHUNK_PNG_SIZE² PNG of topmost non-air block per column (state id → color). */
lc_status lc_chunk_write_top_png(const lc_chunk *c, const char *path);
lc_status lc_map_chunk_write_top_png(const lc_map_chunk *mc, const char *path);
/** Fill rgb with LC_MAP_CHUNK_PNG_SIZE²×3 bytes (caller must allocate). */
lc_status lc_chunk_render_top_rgb(const lc_chunk *c, uint8_t *rgb);

/** One 16×16 map column (same block/color as 16×16 pixels in the chunk PNG). */
typedef struct lc_map_surface_cell {
  uint8_t local_x, local_z;
  int32_t world_x, world_z, world_y;
  int32_t state_id;
  uint8_t map_protocol_id; /* 255 = unmapped */
  uint8_t r, g, b;
} lc_map_surface_cell;

#define LC_MAP_SURFACE_COLUMNS 256
/** world_y when the column has no non-air block (do not confuse with Y=-1). */
#define LC_MAP_SURFACE_NO_Y (-2147483647)

/** Fill 256 cells (lx+lz*16 order) using the same logic as lc_chunk_render_top_rgb. */
lc_status lc_chunk_fill_map_surface(const lc_chunk *c, lc_map_surface_cell *out);
lc_status lc_map_chunk_fill_map_surface(const lc_map_chunk *mc, lc_map_surface_cell *out);
/** Human-readable table to stdout-style stream; returns 0 on success. */
int lc_map_chunk_fprint_map_surface(FILE *f, const lc_map_chunk *mc);

/** Incremental 16×16-chunk tile sheet; directory is typically `<png_dir>X16`. */
typedef struct lc_map_tile_sheet lc_map_tile_sheet;
lc_map_tile_sheet *lc_map_tile_sheet_open(const char *x16_dir);
void lc_map_tile_sheet_close(lc_map_tile_sheet *sheet);
/** Paste this chunk's top surface into the mega-tile and write the tile PNG. */
lc_status lc_map_tile_sheet_blend_map_chunk(lc_map_tile_sheet *sheet, const lc_map_chunk *mc);
int lc_update_light_to_string(const lc_update_light *p, char *buf, size_t buflen);
int lc_update_light_fprint(FILE *f, const lc_update_light *p);
int lc_block_change_to_string(const lc_block_change *p, char *buf, size_t buflen);
int lc_unload_chunk_to_string(const lc_unload_chunk *p, char *buf, size_t buflen);
int lc_tile_entity_data_to_string(const lc_tile_entity_data *p, char *buf, size_t buflen);
int lc_tile_entity_data_fprint(FILE *f, const lc_tile_entity_data *p);
int lc_multi_block_change_to_string(const lc_multi_block_change *p, char *buf, size_t buflen);
int lc_spawn_entity_to_string(const lc_spawn_entity *p, char *buf, size_t buflen);
int lc_entity_metadata_to_string(const lc_entity_metadata *p, char *buf, size_t buflen);
int lc_entity_equipment_to_string(const lc_entity_equipment *p, char *buf, size_t buflen);
int lc_entity_equipment_fprint(FILE *f, const lc_entity_equipment *p);
int lc_entity_destroy_to_string(const lc_entity_destroy *p, char *buf, size_t buflen);
int lc_set_passengers_to_string(const lc_set_passengers *p, char *buf, size_t buflen);
int lc_rel_entity_move_to_string(const lc_rel_entity_move *p, char *buf, size_t buflen);
int lc_entity_move_look_to_string(const lc_entity_move_look *p, char *buf, size_t buflen);
int lc_sync_entity_position_to_string(const lc_sync_entity_position *p, char *buf, size_t buflen);
int lc_entity_velocity_to_string(const lc_entity_velocity *p, char *buf, size_t buflen);
const char *lc_sound_source_name(int32_t source);
int lc_sound_effect_to_string(const lc_sound_effect *p, char *buf, size_t buflen);
int lc_entity_sound_effect_to_string(const lc_entity_sound_effect *p, char *buf, size_t buflen);
int lc_entity_head_rotation_to_string(const lc_entity_head_rotation *p, char *buf, size_t buflen);
const char *lc_entity_attribute_key_name(int32_t key);
int lc_entity_update_attributes_to_string(const lc_entity_update_attributes *p, char *buf,
                                            size_t buflen);
int lc_entity_update_attributes_fprint(FILE *f, const lc_entity_update_attributes *p);
int lc_position_to_string(const lc_position *p, char *buf, size_t buflen);
int lc_position_relatives_to_string(uint32_t flags, char *buf, size_t buflen);
int lc_c2s_position_to_string(const lc_c2s_position *p, char *buf, size_t buflen);
int lc_c2s_position_look_to_string(const lc_c2s_position_look *p, char *buf, size_t buflen);
int lc_c2s_look_to_string(const lc_c2s_look *p, char *buf, size_t buflen);
int lc_c2s_flying_to_string(const lc_c2s_flying *p, char *buf, size_t buflen);
int lc_c2s_teleport_confirm_to_string(const lc_c2s_teleport_confirm *p, char *buf, size_t buflen);
int lc_c2s_block_dig_to_string(const lc_c2s_block_dig *p, char *buf, size_t buflen);
int lc_c2s_player_input_to_string(const lc_c2s_player_input *p, char *buf, size_t buflen);
int lc_c2s_arm_animation_to_string(const lc_c2s_arm_animation *p, char *buf, size_t buflen);
int lc_c2s_container_close_to_string(const lc_c2s_container_close *p, char *buf, size_t buflen);
int lc_c2s_recipe_book_seen_recipe_to_string(const lc_c2s_recipe_book_seen_recipe *p, char *buf, size_t buflen);

int lc_respawn_to_string(const lc_respawn *p, char *buf, size_t buflen);
int lc_world_event_to_string(const lc_world_event *p, char *buf, size_t buflen);
int lc_initialize_world_border_to_string(const lc_initialize_world_border *p, char *buf, size_t buflen);
int lc_custom_payload_to_string(const lc_custom_payload *p, char *buf, size_t buflen);
int lc_feature_flags_to_string(const lc_feature_flags *p, char *buf, size_t buflen);
int lc_select_known_packs_to_string(const lc_select_known_packs *p, char *buf, size_t buflen);
int lc_finish_configuration_to_string(char *buf, size_t buflen);
int lc_bundle_delimiter_to_string(char *buf, size_t buflen);
int lc_step_tick_to_string(const lc_step_tick *p, char *buf, size_t buflen);
int lc_success_to_string(const lc_login_success *p, char *buf, size_t buflen);
int lc_registry_data_to_string(const lc_registry_data *p, char *buf, size_t buflen);
/** Full multi-line dump of all registry entries (+ NBT tree per entry). */
int lc_registry_data_fprint(FILE *f, const lc_registry_data *p);
/** Hex + ASCII wire dump (16 bytes per line). */
int lc_wire_hex_fprint(FILE *f, const uint8_t *wire, size_t len);
/** Indented network-NBT tree. */
int lc_nbt_fprint_wire(FILE *f, const char *indent, const uint8_t *data, size_t len);

#define LC_SIGN_LINES 4

typedef struct lc_sign_text {
  int has_sign;
  char *front[LC_SIGN_LINES];
  char *back[LC_SIGN_LINES];
} lc_sign_text;

void lc_sign_text_free(lc_sign_text *s);
lc_status lc_sign_text_from_nbt(const uint8_t *data, size_t len, lc_sign_text *out);
int lc_chunk_to_string(const lc_chunk *c, char *buf, size_t buflen);

/** Format UUID as 8-4-4-4-12 hex. */
int lc_uuid_to_string(const lc_uuid *u, char *buf, size_t buflen);

#ifdef __cplusplus
}
#endif

#endif /* LIBCHUNK_H */

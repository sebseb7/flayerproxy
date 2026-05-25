#include "libchunk.h"

/*
 * minecraft:block_entity_type registry order (Java 1.21.4).
 * map_chunk blockEntities[].type = registry index, except wire sends index + 1 when index >= 12.
 * Keep in sync with src/sniffer/anvilBlockEntity.js BLOCK_ENTITY_REGISTRY_ORDER.
 */
static const char *LC_BLOCK_ENTITY_REGISTRY[] = {
    "minecraft:furnace",
    "minecraft:chest",
    "minecraft:trapped_chest",
    "minecraft:ender_chest",
    "minecraft:jukebox",
    "minecraft:dispenser",
    "minecraft:dropper",
    "minecraft:sign",
    "minecraft:hanging_sign",
    "minecraft:mob_spawner",
    "minecraft:piston",
    "minecraft:brewing_stand",
    "minecraft:enchanting_table",
    "minecraft:end_portal",
    "minecraft:beacon",
    "minecraft:skull",
    "minecraft:daylight_detector",
    "minecraft:hopper",
    "minecraft:comparator",
    "minecraft:banner",
    "minecraft:structure_block",
    "minecraft:end_gateway",
    "minecraft:command_block",
    "minecraft:shulker", /* registry: shulker_box */
    "minecraft:bed",
    "minecraft:conduit",
    "minecraft:barrel",
    "minecraft:smoker",
    "minecraft:blast_furnace",
    "minecraft:lectern",
    "minecraft:bell",
    "minecraft:jigsaw",
    "minecraft:campfire",
    "minecraft:beehive",
    "minecraft:sculk_sensor",
    "minecraft:calibrated_sculk_sensor",
    "minecraft:sculk_catalyst",
    "minecraft:sculk_shrieker",
    "minecraft:chiseled_bookshelf",
    "minecraft:brushable_block",
    "minecraft:decorated_pot",
    "minecraft:crafter",
    "minecraft:trial_spawner",
    "minecraft:vault",
};

static int lc_block_entity_registry_index(int32_t protocol_type) {
  if (protocol_type < 0) return -1;
  return protocol_type >= 12 ? (int)protocol_type - 1 : (int)protocol_type;
}

const char *lc_block_entity_type_name(int32_t type) {
  int idx = lc_block_entity_registry_index(type);
  if (idx < 0 || idx >= (int)(sizeof(LC_BLOCK_ENTITY_REGISTRY) / sizeof(LC_BLOCK_ENTITY_REGISTRY[0]))) {
    return NULL;
  }
  return LC_BLOCK_ENTITY_REGISTRY[idx];
}

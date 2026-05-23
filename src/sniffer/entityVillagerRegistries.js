'use strict';

/** Minecraft 1.21+ registry order for villager_data metadata indices. */
const VILLAGER_TYPES = [
  'minecraft:desert',
  'minecraft:jungle',
  'minecraft:plains',
  'minecraft:savanna',
  'minecraft:snow',
  'minecraft:swamp',
  'minecraft:taiga',
];

const VILLAGER_PROFESSIONS = [
  'minecraft:none',
  'minecraft:armorer',
  'minecraft:butcher',
  'minecraft:cartographer',
  'minecraft:cleric',
  'minecraft:farmer',
  'minecraft:fisherman',
  'minecraft:fletcher',
  'minecraft:leatherworker',
  'minecraft:librarian',
  'minecraft:mason',
  'minecraft:nitwit',
  'minecraft:shepherd',
  'minecraft:toolsmith',
  'minecraft:weaponsmith',
];

function villagerTypeName(index) {
  return VILLAGER_TYPES[index] ?? 'minecraft:plains';
}

function villagerProfessionName(index) {
  return VILLAGER_PROFESSIONS[index] ?? 'minecraft:none';
}

module.exports = {
  VILLAGER_TYPES,
  VILLAGER_PROFESSIONS,
  villagerTypeName,
  villagerProfessionName,
};

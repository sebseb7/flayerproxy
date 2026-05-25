#ifndef LIBCHUNK_STRUCT_ENTITY_EQUIPMENT_H
#define LIBCHUNK_STRUCT_ENTITY_EQUIPMENT_H

#include "../types.h"

typedef struct lc_entity_equipment {
  int32_t entity_id;
  lc_equipment_arr equipments;
} lc_entity_equipment;

#endif /* LIBCHUNK_STRUCT_ENTITY_EQUIPMENT_H */

#ifndef LIBCHUNK_STRUCT_SOUND_EFFECT_H
#define LIBCHUNK_STRUCT_SOUND_EFFECT_H

#include "../types.h"

typedef struct lc_sound_event_ref {
  char *id; /* direct: resource location string; registry: "#<id>" */
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

#endif /* LIBCHUNK_STRUCT_SOUND_EFFECT_H */

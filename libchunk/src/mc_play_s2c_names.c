#include "mc_play_s2c_names.h"

#include <stddef.h>

#include "mc_play_s2c_names_data.inc"

const char *mc_play_s2c_name(int32_t pkt_id) {
  if (pkt_id < 0 || pkt_id > 255) return NULL;
  return MC_PLAY_S2C_NAMES[pkt_id];
}

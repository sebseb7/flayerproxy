#ifndef MC_PLAY_S2C_NAMES_H
#define MC_PLAY_S2C_NAMES_H

#include <stdint.h>

/** Protocol 773 play clientbound packet label, or NULL if unknown/out of range. */
const char *mc_play_s2c_name(int32_t pkt_id);

#endif

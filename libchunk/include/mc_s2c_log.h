#ifndef MC_S2C_LOG_H
#define MC_S2C_LOG_H

#include <stddef.h>
#include <stdint.h>

/** Log a play-state S2C packet (payload only; id passed separately). */
void mc_log_s2c_play(int32_t pkt_id, const uint8_t *payload, size_t payload_len);

#endif

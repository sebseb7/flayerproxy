#ifndef MC_S2C_LOG_H
#define MC_S2C_LOG_H

#include <stddef.h>
#include <stdint.h>

/** Log a play-state S2C packet (payload only; id passed separately). */
void mc_log_s2c_play(int32_t pkt_id, const uint8_t *payload, size_t payload_len);

typedef enum {
  MC_UPSTREAM_S2C_LOGIN,
  MC_UPSTREAM_S2C_CONFIG,
  MC_UPSTREAM_S2C_PLAY,
} mc_upstream_s2c_phase;

/** Log an S2C packet read from the upstream server (registry fetch / chunk cache). */
void mc_log_upstream_s2c(mc_upstream_s2c_phase phase, int32_t pkt_id, const uint8_t *payload,
                         size_t payload_len, const char *detail);

#endif

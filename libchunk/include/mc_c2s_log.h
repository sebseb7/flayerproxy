#ifndef MC_C2S_LOG_H
#define MC_C2S_LOG_H

#include <stddef.h>
#include <stdint.h>

/** Decode and log a play-state C2S packet payload (no leading packet-id varint). */
void mc_log_c2s_play(const char *username, int32_t pkt_id, const uint8_t *payload, size_t payload_len);

#endif

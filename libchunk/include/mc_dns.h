#ifndef MC_DNS_H
#define MC_DNS_H

#include <stddef.h>
#include <stdint.h>

/** Resolve host/port; follows _minecraft._tcp SRV when port is 25565 and host is not a literal IP. */
int mc_resolve_minecraft(const char *host, uint16_t port_in, char *host_out, size_t host_out_len,
                         uint16_t *port_out);

/** TCP connect with timeout (seconds). Tries SRV-resolved target; prefers IPv4. */
int mc_tcp_connect(const char *host, const char *port_str, int timeout_sec);

#endif

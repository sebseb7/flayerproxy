#define _GNU_SOURCE
#include "mc_dns.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#if defined(__GLIBC__) || defined(__APPLE__)
#define MC_DNS_HAVE_RESOLV 1
#include <arpa/nameser.h>
#include <resolv.h>
#endif

static int host_is_ip(const char *host) {
  struct in_addr a4;
  struct in6_addr a6;
  if (inet_pton(AF_INET, host, &a4) == 1) return 1;
  if (inet_pton(AF_INET6, host, &a6) == 1) return 1;
  return strcmp(host, "localhost") == 0;
}

#ifdef MC_DNS_HAVE_RESOLV
static int parse_srv_target(const unsigned char *answer, int anslen, char *host_out, size_t host_out_len,
                            uint16_t *port_out) {
  ns_msg msg;
  if (ns_initparse(answer, anslen, &msg) < 0) return -1;
  int rr_count = ns_msg_count(msg, ns_s_an);
  for (int i = 0; i < rr_count; i++) {
    ns_rr rr;
    if (ns_parserr(&msg, ns_s_an, i, &rr) < 0) continue;
    if (ns_rr_type(rr) != ns_t_srv) continue;
    const unsigned char *rdata = ns_rr_rdata(rr);
    int rdlen = ns_rr_rdlen(rr);
    if (rdlen < 6) continue;
    uint16_t port = (uint16_t)((rdata[4] << 8) | rdata[5]);
    char name[512];
    if (dn_expand(ns_msg_base(msg), ns_msg_end(msg), rdata + 6, name, sizeof name) < 0) continue;
    size_t n = strlen(name);
    if (n && name[n - 1] == '.') name[n - 1] = '\0';
    snprintf(host_out, host_out_len, "%s", name);
    *port_out = port;
    return 0;
  }
  return -1;
}

static int query_srv(const char *host, char *host_out, size_t host_out_len, uint16_t *port_out) {
  char qname[512];
  snprintf(qname, sizeof qname, "_minecraft._tcp.%s", host);
  unsigned char answer[4096];
  if (res_init() < 0) return -1;
  int n = res_query(qname, C_IN, T_SRV, answer, (int)sizeof answer);
  if (n < 0) return -1;
  return parse_srv_target(answer, n, host_out, host_out_len, port_out);
}
#endif

int mc_resolve_minecraft(const char *host, uint16_t port_in, char *host_out, size_t host_out_len,
                         uint16_t *port_out) {
  if (!host || !host_out || host_out_len == 0 || !port_out) return -1;
  snprintf(host_out, host_out_len, "%s", host);
  *port_out = port_in;
  if (port_in != 25565 || host_is_ip(host)) return 0;
#ifdef MC_DNS_HAVE_RESOLV
  char srv_host[512];
  uint16_t srv_port = port_in;
  if (query_srv(host, srv_host, sizeof srv_host, &srv_port) == 0) {
    snprintf(host_out, host_out_len, "%s", srv_host);
    *port_out = srv_port;
    fprintf(stderr, "SRV %s -> %s:%u\n", host, host_out, (unsigned)*port_out);
    return 0;
  }
#endif
  return 0;
}

static int connect_one(struct addrinfo *ai, int timeout_sec) {
  int fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
  if (fd < 0) return -1;

  int flags = fcntl(fd, F_GETFL, 0);
  if (flags >= 0) fcntl(fd, F_SETFL, flags | O_NONBLOCK);

  int rc = connect(fd, ai->ai_addr, ai->ai_addrlen);
  if (rc == 0) {
    if (flags >= 0) fcntl(fd, F_SETFL, flags);
    return fd;
  }
  if (errno != EINPROGRESS) {
    close(fd);
    return -1;
  }

  struct pollfd pfd = {.fd = fd, .events = POLLOUT};
  rc = poll(&pfd, 1, timeout_sec * 1000);
  if (rc <= 0) {
    close(fd);
    return -1;
  }
  int err = 0;
  socklen_t elen = sizeof err;
  if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &elen) != 0 || err != 0) {
    close(fd);
    return -1;
  }
  if (flags >= 0) fcntl(fd, F_SETFL, flags);
  return fd;
}

int mc_tcp_connect(const char *host, const char *port_str, int timeout_sec) {
  char resolved[512];
  uint16_t port_u = (uint16_t)atoi(port_str);
  if (mc_resolve_minecraft(host, port_u, resolved, sizeof resolved, &port_u) != 0) return -1;

  char port_buf[16];
  snprintf(port_buf, sizeof port_buf, "%u", (unsigned)port_u);

  struct addrinfo hints, *res = NULL;
  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;

  int gai = getaddrinfo(resolved, port_buf, &hints, &res);
  if (gai != 0) {
    fprintf(stderr, "getaddrinfo(%s): %s\n", resolved, gai_strerror(gai));
    return -1;
  }

  struct addrinfo *ipv4_first[64];
  struct addrinfo *other[64];
  size_t n4 = 0, no = 0;
  for (struct addrinfo *ai = res; ai; ai = ai->ai_next) {
    if (ai->ai_family == AF_INET && n4 < sizeof ipv4_first / sizeof ipv4_first[0])
      ipv4_first[n4++] = ai;
    else if (no < sizeof other / sizeof other[0])
      other[no++] = ai;
  }

  int fd = -1;
  for (size_t pass = 0; pass < 2 && fd < 0; pass++) {
    struct addrinfo **list = pass == 0 ? ipv4_first : other;
    size_t count = pass == 0 ? n4 : no;
    for (size_t i = 0; i < count; i++) {
      fd = connect_one(list[i], timeout_sec);
      if (fd >= 0) break;
    }
  }

  freeaddrinfo(res);
  if (fd < 0) fprintf(stderr, "connect to %s:%s failed: %s\n", resolved, port_buf, strerror(errno));
  return fd;
}

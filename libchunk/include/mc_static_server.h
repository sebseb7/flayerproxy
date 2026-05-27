#ifndef MC_STATIC_SERVER_H
#define MC_STATIC_SERVER_H

#include "mc_auth.h"
#include "mc_server_common.h"

typedef struct mc_static_server_opts {
  const char *host;
  int port;
  mc_gamemode gamemode;
  const mc_auth_ops *auth;
} mc_static_server_opts;

int mc_static_server_start(const mc_static_server_opts *opts);
void mc_static_server_stop(void);

#endif

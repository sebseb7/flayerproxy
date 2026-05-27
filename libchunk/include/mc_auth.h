#ifndef MC_AUTH_H
#define MC_AUTH_H

#include "mc_server_common.h"

typedef struct mc_auth_ops {
  /** Handle C2S login_start; populate cli uuid/username. */
  int (*on_login_start)(mc_client *cli, const uint8_t *payload, size_t len);
  /** Send login_success (+ optional compress). Return 0 on success. */
  int (*send_login_success)(mc_client *cli);
} mc_auth_ops;

extern const mc_auth_ops mc_auth_offline;

#endif

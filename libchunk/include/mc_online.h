#ifndef MC_ONLINE_H
#define MC_ONLINE_H

#include <stddef.h>
#include <stdint.h>

#include "mc_conn.h"

typedef struct mc_online_creds {
  const char *access_token;
  const char *profile_id;
} mc_online_creds;

/** Load MC_ACCESS_TOKEN + MC_PROFILE_ID from environment. Returns 0 if both set. */
int mc_online_creds_from_env(mc_online_creds *out);

/**
 * Run libchunk/scripts/msa_token.js (prismarine-auth) using username.
 * Writes token/profile into heap strings; caller frees *access_token and *profile_id.
 */
int mc_online_creds_from_msa(const char *username, char **access_token, char **profile_id);

typedef struct mc_encryption_begin {
  char server_id[256];
  uint8_t *public_key;
  size_t public_key_len;
  uint8_t *verify_token;
  size_t verify_token_len;
  int should_authenticate;
} mc_encryption_begin;

int mc_parse_encryption_begin(const uint8_t *payload, size_t len, mc_encryption_begin *out);
void mc_encryption_begin_free(mc_encryption_begin *p);

/** Answer encryption_begin and enable AES on conn. */
int mc_online_handle_encryption_begin(mc_conn *conn, const mc_encryption_begin *req,
                                      const mc_online_creds *creds);

#endif

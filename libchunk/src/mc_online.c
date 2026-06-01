#define _GNU_SOURCE
#include "mc_online.h"

#include "internal.h"
#include "mc_packet_ids.h"
#include "mc_wire.h"

#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/rsa.h>
#include <openssl/ssl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zlib.h>

static void mc_hex_digest_java(const unsigned char *hash, size_t hash_len, int negative, char *out,
                               size_t out_len) {
  unsigned char buf[32];
  size_t n = hash_len < sizeof buf ? hash_len : sizeof buf;
  memcpy(buf, hash, n);
  if (negative) {
    int carry = 1;
    for (int i = (int)n - 1; i >= 0; i--) {
      unsigned v = (unsigned)(~buf[i] & 0xff);
      if (carry) {
        if (v == 0xff) {
          buf[i] = 0;
          carry = 1;
        } else {
          buf[i] = (uint8_t)(v + 1);
          carry = 0;
        }
      } else {
        buf[i] = (uint8_t)v;
      }
    }
  }
  char hex[128];
  for (size_t i = 0; i < n; i++) snprintf(hex + i * 2, 3, "%02x", buf[i]);
  const char *p = hex;
  while (*p == '0' && p[1]) p++;
  if (negative && out_len > 1) {
    out[0] = '-';
    snprintf(out + 1, out_len - 1, "%.*s", (int)(out_len - 2), p);
  } else {
    snprintf(out, out_len, "%.*s", (int)(out_len - 1), p);
  }
}

static int mc_server_id_hash(const char *server_id, const uint8_t *shared_secret, size_t secret_len,
                              const uint8_t *public_key, size_t public_key_len, char *out,
                              size_t out_len) {
  EVP_MD_CTX *ctx = EVP_MD_CTX_new();
  if (!ctx) return -1;
  if (EVP_DigestInit_ex(ctx, EVP_sha1(), NULL) != 1) {
    EVP_MD_CTX_free(ctx);
    return -1;
  }
  if (server_id) EVP_DigestUpdate(ctx, server_id, strlen(server_id));
  EVP_DigestUpdate(ctx, shared_secret, secret_len);
  EVP_DigestUpdate(ctx, public_key, public_key_len);
  unsigned char hash[EVP_MAX_MD_SIZE];
  unsigned int hash_len = 0;
  if (EVP_DigestFinal_ex(ctx, hash, &hash_len) != 1) {
    EVP_MD_CTX_free(ctx);
    return -1;
  }
  EVP_MD_CTX_free(ctx);
  int negative = hash_len > 0 && (hash[0] & 0x80) != 0;
  mc_hex_digest_java(hash, hash_len, negative, out, out_len);
  return 0;
}

static int mc_mojang_join(const char *access_token, const char *profile_id, const char *server_id_hash) {
  char body[4096];
  int blen = snprintf(body, sizeof body,
                      "{\"accessToken\":\"%s\",\"selectedProfile\":\"%s\",\"serverId\":\"%s\"}",
                      access_token, profile_id, server_id_hash);
  if (blen <= 0 || (size_t)blen >= sizeof body) return -1;

  SSL_library_init();
  SSL_CTX *ssl_ctx = SSL_CTX_new(TLS_client_method());
  if (!ssl_ctx) return -1;
  BIO *bio = BIO_new_ssl_connect(ssl_ctx);
  if (!bio) {
    SSL_CTX_free(ssl_ctx);
    return -1;
  }
  BIO_set_conn_hostname(bio, "sessionserver.mojang.com:443");
  if (BIO_do_connect(bio) <= 0) {
    BIO_free_all(bio);
    SSL_CTX_free(ssl_ctx);
    return -1;
  }

  char req[8192];
  int rlen = snprintf(req, sizeof req,
                      "POST /session/minecraft/join HTTP/1.1\r\n"
                      "Host: sessionserver.mojang.com\r\n"
                      "User-Agent: flayerproxy-mc_reference_client\r\n"
                      "Content-Type: application/json\r\n"
                      "Content-Length: %d\r\n"
                      "Connection: close\r\n\r\n%s",
                      blen, body);
  if (rlen <= 0 || BIO_write(bio, req, rlen) != rlen) {
    BIO_free_all(bio);
    SSL_CTX_free(ssl_ctx);
    return -1;
  }

  char resp[4096];
  int total = 0;
  for (;;) {
    int n = BIO_read(bio, resp + total, (int)sizeof resp - total - 1);
    if (n <= 0) break;
    total += n;
    if (total >= (int)sizeof resp - 1) break;
  }
  resp[total > 0 ? total : 0] = '\0';
  BIO_free_all(bio);
  SSL_CTX_free(ssl_ctx);

  if (!strstr(resp, "HTTP/1.1 204") && !strstr(resp, "HTTP/1.0 204") && strstr(resp, "error")) {
    fprintf(stderr, "Mojang join failed: %.200s\n", resp);
    return -1;
  }
  if (!strstr(resp, " 204")) {
    fprintf(stderr, "Mojang join unexpected response: %.200s\n", resp);
    return -1;
  }
  return 0;
}

int mc_online_creds_from_env(mc_online_creds *out) {
  if (!out) return -1;
  const char *tok = getenv("MC_ACCESS_TOKEN");
  const char *prof = getenv("MC_PROFILE_ID");
  if (!tok || !*tok || !prof || !*prof) return -1;
  out->access_token = tok;
  out->profile_id = prof;
  return 0;
}

static char *json_field_dup(const char *line, const char *key) {
  char pat[64];
  snprintf(pat, sizeof pat, "\"%s\":\"", key);
  const char *p = strstr(line, pat);
  if (!p) return NULL;
  p += strlen(pat);
  const char *end = strchr(p, '"');
  if (!end) return NULL;
  size_t n = (size_t)(end - p);
  char *out = (char *)malloc(n + 1);
  if (!out) return NULL;
  memcpy(out, p, n);
  out[n] = '\0';
  return out;
}

static int run_msa_script(const char *username, char **access_token, char **profile_id) {
  const char *root = getenv("FLAYERPROXY_ROOT");
  if (!root || !*root) root = ".";
  char cmd[1024];
  snprintf(cmd, sizeof cmd, "node \"%s/libchunk/scripts/msa_token.js\" \"%s\" 2>&1", root, username);
  FILE *fp = popen(cmd, "r");
  if (!fp) return -1;
  char line[8192];
  if (!fgets(line, sizeof line, fp)) {
    pclose(fp);
    return -1;
  }
  int status = pclose(fp);
  if (status != 0) {
    fprintf(stderr, "%s", line);
    return -1;
  }
  *access_token = json_field_dup(line, "accessToken");
  *profile_id = json_field_dup(line, "profileId");
  if (!*access_token || !*profile_id) {
    free(*access_token);
    free(*profile_id);
    *access_token = NULL;
    *profile_id = NULL;
    return -1;
  }
  return 0;
}

int mc_online_creds_from_msa(const char *username, char **access_token, char **profile_id) {
  return run_msa_script(username, access_token, profile_id);
}

int mc_parse_encryption_begin(const uint8_t *payload, size_t len, mc_encryption_begin *out) {
  if (!payload || !out) return -1;
  memset(out, 0, sizeof *out);
  lc_buf b;
  lc_buf_init(&b, payload, len);
  char *server_id = NULL;
  if (lc_buf_read_string(&b, &server_id) != LC_OK) return -1;
  snprintf(out->server_id, sizeof out->server_id, "%s", server_id ? server_id : "");
  free(server_id);

  lc_byte_buf pk;
  memset(&pk, 0, sizeof pk);
  if (lc_buf_read_byte_array(&b, &pk) != LC_OK) return -1;
  out->public_key = pk.data;
  out->public_key_len = pk.len;

  lc_byte_buf vt;
  memset(&vt, 0, sizeof vt);
  if (lc_buf_read_byte_array(&b, &vt) != LC_OK) {
    mc_encryption_begin_free(out);
    return -1;
  }
  out->verify_token = vt.data;
  out->verify_token_len = vt.len;

  uint8_t auth = 1;
  if (lc_buf_read_bool(&b, &auth) != LC_OK) auth = 1;
  out->should_authenticate = auth ? 1 : 0;
  return 0;
}

void mc_encryption_begin_free(mc_encryption_begin *p) {
  if (!p) return;
  free(p->public_key);
  free(p->verify_token);
  memset(p, 0, sizeof *p);
}

static int mc_rsa_encrypt(const uint8_t *pub, size_t pub_len, const uint8_t *in, size_t in_len,
                          uint8_t **out, size_t *out_len) {
  const unsigned char *p = pub;
  EVP_PKEY *evp = d2i_PUBKEY(NULL, &p, (long)pub_len);
  if (!evp) return -1;
  EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new(evp, NULL);
  if (!ctx) {
    EVP_PKEY_free(evp);
    return -1;
  }
  if (EVP_PKEY_encrypt_init(ctx) != 1 || EVP_PKEY_CTX_set_rsa_padding(ctx, RSA_PKCS1_PADDING) != 1) {
    EVP_PKEY_CTX_free(ctx);
    EVP_PKEY_free(evp);
    return -1;
  }
  size_t outsz = 0;
  if (EVP_PKEY_encrypt(ctx, NULL, &outsz, in, in_len) != 1) {
    EVP_PKEY_CTX_free(ctx);
    EVP_PKEY_free(evp);
    return -1;
  }
  uint8_t *buf = (uint8_t *)malloc(outsz);
  if (!buf) {
    EVP_PKEY_CTX_free(ctx);
    EVP_PKEY_free(evp);
    return -1;
  }
  if (EVP_PKEY_encrypt(ctx, buf, &outsz, in, in_len) != 1) {
    free(buf);
    EVP_PKEY_CTX_free(ctx);
    EVP_PKEY_free(evp);
    return -1;
  }
  EVP_PKEY_CTX_free(ctx);
  EVP_PKEY_free(evp);
  *out = buf;
  *out_len = outsz;
  return 0;
}

int mc_online_handle_encryption_begin(mc_conn *conn, const mc_encryption_begin *req,
                                      const mc_online_creds *creds) {
  if (!conn || !req) return -1;

  uint8_t shared_secret[16];
  if (RAND_bytes(shared_secret, 16) != 1) return -1;

  if (req->should_authenticate && creds && creds->access_token && creds->profile_id) {
    char server_hash[128];
    if (mc_server_id_hash(req->server_id, shared_secret, sizeof shared_secret, req->public_key,
                          req->public_key_len, server_hash, sizeof server_hash) != 0)
      return -1;
    fprintf(stderr, "Mojang session join...\n");
    if (mc_mojang_join(creds->access_token, creds->profile_id, server_hash) != 0) return -1;
  } else if (req->should_authenticate) {
    fprintf(stderr,
            "online-mode server requires auth; set MC_ACCESS_TOKEN + MC_PROFILE_ID or run from repo root for MSA login\n");
    return -1;
  }

  uint8_t *enc_secret = NULL;
  size_t enc_secret_len = 0;
  uint8_t *enc_token = NULL;
  size_t enc_token_len = 0;
  if (mc_rsa_encrypt(req->public_key, req->public_key_len, shared_secret, sizeof shared_secret,
                     &enc_secret, &enc_secret_len) != 0 ||
      mc_rsa_encrypt(req->public_key, req->public_key_len, req->verify_token, req->verify_token_len,
                     &enc_token, &enc_token_len) != 0) {
    free(enc_secret);
    free(enc_token);
    return -1;
  }

  mc_buf payload;
  memset(&payload, 0, sizeof payload);
  if (mc_buf_varint(&payload, (int32_t)enc_secret_len) != LC_OK ||
      mc_buf_write(&payload, enc_secret, enc_secret_len) != LC_OK ||
      mc_buf_varint(&payload, (int32_t)enc_token_len) != LC_OK ||
      mc_buf_write(&payload, enc_token, enc_token_len) != LC_OK) {
    free(enc_secret);
    free(enc_token);
    mc_buf_free(&payload);
    return -1;
  }
  free(enc_secret);
  free(enc_token);

  if (mc_conn_send_frame(conn, MC_PKT_C2S_LOGIN_ENCRYPTION_BEGIN, payload.data, payload.len) != 0) {
    mc_buf_free(&payload);
    return -1;
  }
  mc_buf_free(&payload);

  if (mc_conn_set_encryption(conn, shared_secret) != 0) return -1;
  fprintf(stderr, "encryption enabled\n");
  return 0;
}

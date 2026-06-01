#ifndef MC_CONN_H
#define MC_CONN_H

#include <stddef.h>
#include <stdint.h>

typedef struct mc_conn mc_conn;

struct mc_conn {
  int fd;
  int32_t compress_threshold;
  int encrypted;
  void *enc_ctx;
  void *dec_ctx;
  uint8_t *plain;
  size_t plain_len;
  size_t plain_cap;
  uint8_t sock_buf[8192];
  size_t sock_have;
};

void mc_conn_init(mc_conn *c, int fd);
void mc_conn_free(mc_conn *c);

void mc_conn_set_compress(mc_conn *c, int32_t threshold);
int mc_conn_set_encryption(mc_conn *c, const uint8_t shared_secret[16]);

int mc_conn_send_frame(mc_conn *c, int32_t pkt_id, const uint8_t *payload, size_t payload_len);
int mc_conn_read_packet(mc_conn *c, uint8_t **out, size_t *out_len, int32_t *pkt_id);

#endif

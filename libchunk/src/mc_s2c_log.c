#include "mc_s2c_log.h"

#include "internal.h"
#include "libchunk.h"
#include "mc_log.h"
#include "mc_packet_ids.h"
#include "mc_play_s2c_names.h"

#include <stdio.h>
#include <string.h>

static const char *s2c_play_name(int32_t pkt_id) { return mc_play_s2c_name(pkt_id); }
/* Good for: Log map_chunk light mask summary to stderr.
 * Callers: mc_s2c_log.c (same file).
 */

static void log_map_chunk_light(const uint8_t *payload, size_t payload_len) {
  lc_map_chunk mc;
  memset(&mc, 0, sizeof mc);
  if (lc_parse_map_chunk(payload, payload_len, &mc) != LC_OK) {
    MC_LOGI("static_server", "  map_chunk light: (embedded light_update layout; parse failed)");
    return;
  }
  MC_LOGI("static_server",
          "  map_chunk light (embedded light_update): chunk=(%d,%d) skyMask=%zu emptySky=%zu "
          "blockMask=%zu emptyBlock=%zu skySections=%zu blockSections=%zu",
          mc.x, mc.z, mc.sky_light_mask.count, mc.empty_sky_light_mask.count, mc.block_light_mask.count,
          mc.empty_block_light_mask.count, mc.sky_light.row_count, mc.block_light.row_count);
  if (mc.sky_light_mask.count) {
    MC_LOGI("static_server", "    sky_mask[0]=0x%llx empty_sky[0]=0x%llx",
            (unsigned long long)mc.sky_light_mask.values[0],
            mc.empty_sky_light_mask.count ? (unsigned long long)mc.empty_sky_light_mask.values[0] : 0ULL);
  }
  lc_map_chunk_free(&mc);
}
/* Good for: Log update_light mask summary to stderr.
 * Callers: mc_s2c_log.c (same file).
 */

static void log_light_update(const uint8_t *payload, size_t payload_len) {
  lc_update_light ul;
  memset(&ul, 0, sizeof ul);
  if (lc_parse_update_light(payload, payload_len, &ul) != LC_OK) {
    MC_LOGI("static_server", "  light_update: parse failed len=%zu", payload_len);
    return;
  }
  MC_LOGI("static_server",
          "  light_update: chunk=(%d,%d) skyMask=%zu emptySky=%zu blockMask=%zu emptyBlock=%zu "
          "skySections=%zu blockSections=%zu",
          ul.chunk_x, ul.chunk_z, ul.sky_light_mask.count, ul.empty_sky_light_mask.count,
          ul.block_light_mask.count, ul.empty_block_light_mask.count, ul.sky_light.row_count,
          ul.block_light.row_count);
  lc_update_light_free(&ul);
}
/* Good for: Colored stderr logging for mc_* tools.
 * Callers: mc_wire_templates.c.
 */

void mc_log_s2c_play(int32_t pkt_id, const uint8_t *payload, size_t payload_len) {
  const char *name = s2c_play_name(pkt_id);
  if (!name) {
    MC_LOGI("static_server", "S2C play 0x%02x len=%zu", pkt_id, payload_len);
    return;
  }
  MC_LOGI("static_server", "S2C %s 0x%02x len=%zu", name, pkt_id, payload_len);
  if (pkt_id == MC_PKT_PLAY_MAP_CHUNK) {
    log_map_chunk_light(payload, payload_len);
  } else if (pkt_id == MC_PKT_PLAY_LIGHT_UPDATE) {
    log_light_update(payload, payload_len);
  }
}

static const char *upstream_phase_name(mc_upstream_s2c_phase phase) {
  switch (phase) {
  case MC_UPSTREAM_S2C_LOGIN:
    return "login";
  case MC_UPSTREAM_S2C_CONFIG:
    return "config";
  case MC_UPSTREAM_S2C_PLAY:
    return "play";
  default:
    return "?";
  }
}

static const char *upstream_login_pkt_name(int32_t pkt_id) {
  switch (pkt_id) {
  case MC_PKT_LOGIN_DISCONNECT:
    return "disconnect";
  case MC_PKT_LOGIN_ENCRYPTION_BEGIN:
    return "encryption_begin";
  case MC_PKT_LOGIN_SUCCESS:
    return "success";
  case MC_PKT_LOGIN_COMPRESS:
    return "compress";
  default:
    return NULL;
  }
}

static const char *upstream_config_pkt_name(int32_t pkt_id) {
  switch (pkt_id) {
  case MC_PKT_CFG_FINISH:
    return "finish_configuration";
  case MC_PKT_CFG_KEEP_ALIVE:
    return "keep_alive";
  case MC_PKT_CFG_PING:
    return "ping";
  case MC_PKT_CFG_REGISTRY_DATA:
    return "registry_data";
  case MC_PKT_CFG_SELECT_KNOWN_PACKS:
    return "select_known_packs";
  case MC_PKT_CFG_FEATURE_FLAGS:
    return "feature_flags";
  case MC_PKT_CFG_RESET_CHAT:
    return "reset_chat";
  case MC_PKT_COMMON_CUSTOM_PAYLOAD:
    return "custom_payload";
  case MC_PKT_COMMON_UPDATE_TAGS:
    return "update_tags";
  default:
    return NULL;
  }
}

static int upstream_pkt_verbose(mc_upstream_s2c_phase phase, int32_t pkt_id) {
  if (phase == MC_UPSTREAM_S2C_LOGIN) return 1;
  if (phase == MC_UPSTREAM_S2C_CONFIG && pkt_id == MC_PKT_CFG_KEEP_ALIVE) return 0;
  if (phase == MC_UPSTREAM_S2C_PLAY &&
      (pkt_id == MC_PKT_PLAY_KEEP_ALIVE || pkt_id == MC_PKT_PLAY_LIGHT_UPDATE)) {
    return 0;
  }
  return 1;
}

static const char *upstream_pkt_name(mc_upstream_s2c_phase phase, int32_t pkt_id) {
  if (phase == MC_UPSTREAM_S2C_LOGIN) return upstream_login_pkt_name(pkt_id);
  if (phase == MC_UPSTREAM_S2C_CONFIG) return upstream_config_pkt_name(pkt_id);
  return s2c_play_name(pkt_id);
}

void mc_log_upstream_s2c(mc_upstream_s2c_phase phase, int32_t pkt_id, const uint8_t *payload,
                         size_t payload_len, const char *detail) {
  (void)payload;
  const char *phase_name = upstream_phase_name(phase);
  const char *pkt_name = upstream_pkt_name(phase, pkt_id);
  const int verbose = upstream_pkt_verbose(phase, pkt_id);
  const mc_log_level level = verbose ? MC_LOG_INFO : MC_LOG_DEBUG;

  if (detail && detail[0]) {
    if (pkt_name) {
      mc_log_msg(level, "upstream", "%s S2C 0x%02x %s %s len=%zu", phase_name, pkt_id, pkt_name, detail,
                 payload_len);
    } else {
      mc_log_msg(level, "upstream", "%s S2C 0x%02x %s len=%zu", phase_name, pkt_id, detail, payload_len);
    }
    return;
  }

  if (pkt_name) {
    mc_log_msg(level, "upstream", "%s S2C 0x%02x %s len=%zu", phase_name, pkt_id, pkt_name, payload_len);
  } else {
    mc_log_msg(level, "upstream", "%s S2C 0x%02x len=%zu", phase_name, pkt_id, payload_len);
  }
}

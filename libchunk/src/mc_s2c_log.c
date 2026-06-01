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

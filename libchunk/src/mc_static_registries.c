#define _POSIX_C_SOURCE 200809L
#include "mc_log.h"
#include "mc_static_registries.h"

#include "internal.h"
#include "libchunk.h"
#include "mc_packet_ids.h"
#include "mc_registry_capture.h"
#include "mc_server_common.h"
#include "mc_static_registries_data.h"
#include "mc_wire.h"
#include "packets_write.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

static mc_static_registry_fetch g_fetch;
static int g_fetch_enabled;

static lc_registry_data *g_registries;
static size_t g_registry_count;
static lc_update_tags g_tags;
static int g_tags_loaded;

static mc_reg_sync_step *g_steps;
static size_t g_step_count;
static int g_owns_steps;
static int g_send_reset_chat_before_finish;

static uint8_t *g_packs_key;
static size_t g_packs_key_len;

static int g_cached_login_valid;
static uint8_t g_login_hardcore;
static char **g_login_world_names;
static size_t g_login_world_name_count;
static int32_t g_login_max_players;
static int32_t g_login_view_distance;
static int32_t g_login_simulation_distance;
static uint8_t g_login_reduced_debug_info;
static uint8_t g_login_enable_respawn_screen;
static uint8_t g_login_do_limited_crafting;
static lc_spawn_info g_login_world_state;
static uint8_t g_login_enforces_secure_chat;

static int g_cached_position_valid;
static lc_position g_cached_position;

typedef enum {
  REG_STATE_EMPTY = 0,
  REG_STATE_LOADING,
  REG_STATE_CONFIG_READY,
  REG_STATE_READY,
  REG_STATE_FAILED,
} reg_load_state;

static pthread_mutex_t g_load_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_load_cond = PTHREAD_COND_INITIALIZER;
static reg_load_state g_load_state = REG_STATE_EMPTY;

static uint8_t *dup_bytes(const uint8_t *src, size_t len) {
  if (len == 0) return (uint8_t *)calloc(1, 1);
  uint8_t *p = (uint8_t *)malloc(len);
  if (!p) return NULL;
  memcpy(p, src, len);
  return p;
}

static int packs_key_matches(const uint8_t *packs, size_t len) {
  return g_packs_key && g_packs_key_len == len && memcmp(g_packs_key, packs, len) == 0;
}

static int send_registry_payload(int fd, const uint8_t *data, size_t len) {
  return mc_send_frame(fd, MC_PKT_CFG_REGISTRY_DATA, data, len);
}

static int send_tags_payload(int fd, const uint8_t *data, size_t len) {
  return mc_send_frame(fd, MC_PKT_COMMON_UPDATE_TAGS, data, len);
}

static int send_reset_chat(int fd) {
  return mc_send_frame(fd, MC_PKT_CFG_RESET_CHAT, NULL, 0);
}

void mc_static_registries_set_fetch(const mc_static_registry_fetch *fetch) {
  g_fetch_enabled = 0;
  memset(&g_fetch, 0, sizeof g_fetch);
  if (fetch && fetch->host && fetch->host[0] && fetch->port > 0) {
    g_fetch = *fetch;
    g_fetch_enabled = 1;
  }
}

static void clear_loaded_data(void) {
  if (g_registries) {
    for (size_t i = 0; i < g_registry_count; i++) lc_registry_data_free(&g_registries[i]);
    free(g_registries);
    g_registries = NULL;
  }
  g_registry_count = 0;
  if (g_tags_loaded) lc_update_tags_free(&g_tags);
  g_tags_loaded = 0;
  if (g_steps) {
    if (g_owns_steps) {
      mc_registry_capture_result cap = {.steps = g_steps, .step_count = g_step_count};
      mc_registry_capture_result_free(&cap);
    } else {
      free(g_steps);
    }
    g_steps = NULL;
  }
  g_step_count = 0;
  g_owns_steps = 0;
  g_send_reset_chat_before_finish = 0;
  free(g_packs_key);
  g_packs_key = NULL;
  g_packs_key_len = 0;
  if (g_login_world_names) {
    for (size_t i = 0; i < g_login_world_name_count; i++) free(g_login_world_names[i]);
    free(g_login_world_names);
    g_login_world_names = NULL;
  }
  g_login_world_name_count = 0;
  g_cached_login_valid = 0;
  lc_spawn_info_free(&g_login_world_state);
  memset(&g_login_world_state, 0, sizeof g_login_world_state);
  g_cached_position_valid = 0;
  memset(&g_cached_position, 0, sizeof g_cached_position);
}

static int build_steps_from_embedded(void) {
  g_step_count = mc_static_registry_blob_count + 1;
  if (mc_static_registry_blob_count <= 12) g_step_count++;
  g_steps = (mc_reg_sync_step *)calloc(g_step_count, sizeof(mc_reg_sync_step));
  if (!g_steps) return -1;
  g_owns_steps = 0;
  size_t n = 0;
  for (size_t i = 0; i < mc_static_registry_blob_count; i++) {
    if (i == 12) {
      g_steps[n].kind = MC_REG_SYNC_TAGS;
      g_steps[n].data = (uint8_t *)mc_static_tags.data;
      g_steps[n].len = mc_static_tags.len;
      snprintf(g_steps[n].label, sizeof g_steps[n].label, "update_tags");
      n++;
    }
    const mc_static_blob *blob = &mc_static_registry_blobs[i];
    g_steps[n].kind = MC_REG_SYNC_REGISTRY;
    g_steps[n].data = (uint8_t *)blob->data;
    g_steps[n].len = blob->len;
    snprintf(g_steps[n].label, sizeof g_steps[n].label, "%s", blob->label ? blob->label : "registry");
    n++;
  }
  if (mc_static_registry_blob_count <= 12) {
    g_steps[n].kind = MC_REG_SYNC_TAGS;
    g_steps[n].data = (uint8_t *)mc_static_tags.data;
    g_steps[n].len = mc_static_tags.len;
    snprintf(g_steps[n].label, sizeof g_steps[n].label, "update_tags");
    n++;
  }
  g_step_count = n;
  g_send_reset_chat_before_finish = 1;
  return 0;
}

static int load_from_remote_capture(const mc_registry_capture_result *cap);

static void ingest_join_capture(void) {
  if (!g_steps) return;
  for (size_t i = 0; i < g_step_count; i++) {
    const mc_reg_sync_step *step = &g_steps[i];
    if (step->kind != MC_REG_SYNC_PLAY) continue;
    if (step->pkt_id == MC_PKT_PLAY_LOGIN && !g_cached_login_valid) {
      lc_play_login parsed;
      char **wnames = NULL;
      size_t wcount = 0;
      if (lc_parse_play_login(step->data, step->len, &parsed, &wnames, &wcount) != LC_OK) continue;
      g_login_hardcore = parsed.hardcore;
      g_login_world_names = wnames;
      g_login_world_name_count = wcount;
      g_login_max_players = parsed.max_players;
      g_login_view_distance = parsed.view_distance;
      g_login_simulation_distance = parsed.simulation_distance;
      g_login_reduced_debug_info = parsed.reduced_debug_info;
      g_login_enable_respawn_screen = parsed.enable_respawn_screen;
      g_login_do_limited_crafting = parsed.do_limited_crafting;
      g_login_enforces_secure_chat = parsed.enforces_secure_chat;
      g_login_world_state = parsed.world_state;
      parsed.world_state.name = NULL;
      parsed.world_state.death_dimension_name = NULL;
      g_cached_login_valid = 1;
      MC_LOGI("static_server",
              "registry fetch: login template (dim=%d name=%s gamemode=%d seaLevel=%d view=%d sim=%d)",
              g_login_world_state.dimension, g_login_world_state.name ? g_login_world_state.name : "?",
              g_login_world_state.gamemode, g_login_world_state.sea_level, g_login_view_distance,
              g_login_simulation_distance);
    } else if (step->pkt_id == MC_PKT_PLAY_POSITION && !g_cached_position_valid) {
      if (lc_parse_position(step->data, step->len, &g_cached_position) != LC_OK) continue;
      g_cached_position_valid = 1;
      MC_LOGI("static_server", "registry fetch: position template (%.3f, %.3f, %.3f)", g_cached_position.x,
              g_cached_position.y, g_cached_position.z);
    }
  }
}

typedef struct fetch_thread_arg {
  uint8_t *client_packs;
  size_t client_packs_len;
  mc_registry_capture_result result;
  int rc;
} fetch_thread_arg;

static void on_config_ready(const mc_registry_capture_result *cap, int ok, void *v) {
  fetch_thread_arg *a = (fetch_thread_arg *)v;
  if (!ok) return;

  pthread_mutex_lock(&g_load_mutex);
  if (load_from_remote_capture(cap) != 0) {
    g_load_state = REG_STATE_FAILED;
    pthread_cond_broadcast(&g_load_cond);
    pthread_mutex_unlock(&g_load_mutex);
    return;
  }
  g_packs_key = dup_bytes(a->client_packs, a->client_packs_len);
  if (!g_packs_key && a->client_packs_len > 0) {
    clear_loaded_data();
    g_load_state = REG_STATE_FAILED;
    pthread_cond_broadcast(&g_load_cond);
    pthread_mutex_unlock(&g_load_mutex);
    return;
  }
  g_packs_key_len = a->client_packs_len;
  g_load_state = REG_STATE_CONFIG_READY;
  MC_LOGI("static_server", "registry fetch: config cached (%zu registries + tags), continuing play capture",
          g_registry_count);
  pthread_cond_broadcast(&g_load_cond);
  pthread_mutex_unlock(&g_load_mutex);
}

static void *registry_fetch_thread(void *arg) {
  fetch_thread_arg *a = (fetch_thread_arg *)arg;
  mc_registry_capture_config cfg = {
      .host = g_fetch.host,
      .port = g_fetch.port,
      .username = g_fetch.username ? g_fetch.username : "FlayerBot",
      .client_known_packs = a->client_packs,
      .client_known_packs_len = a->client_packs_len,
  };
  memset(&a->result, 0, sizeof a->result);
  a->rc = mc_registry_capture_configuration(&cfg, &a->result, on_config_ready, a);

  pthread_mutex_lock(&g_load_mutex);
  if (a->rc == 0 && g_load_state == REG_STATE_CONFIG_READY) {
    g_step_count = a->result.step_count;
    ingest_join_capture();
    g_load_state = REG_STATE_READY;
    MC_LOGI("static_server", "registry fetch: play join cached (%zu sync steps total)", g_step_count);
  } else if (g_load_state == REG_STATE_CONFIG_READY) {
    g_step_count = a->result.step_count;
    ingest_join_capture();
    MC_LOGW("static_server", "registry fetch: play join failed; config cache kept, play defaults on join");
    g_load_state = REG_STATE_READY;
  } else if (g_load_state == REG_STATE_LOADING) {
    mc_registry_capture_result_free(&a->result);
    g_load_state = REG_STATE_FAILED;
  }
  pthread_cond_broadcast(&g_load_cond);
  pthread_mutex_unlock(&g_load_mutex);

  free(a->client_packs);
  free(a);
  return NULL;
}

static int start_remote_fetch(const uint8_t *client_packs, size_t client_packs_len) {
  fetch_thread_arg *arg = (fetch_thread_arg *)calloc(1, sizeof *arg);
  if (!arg) return -1;
  arg->client_packs = dup_bytes(client_packs, client_packs_len);
  if (!arg->client_packs && client_packs_len > 0) {
    free(arg);
    return -1;
  }
  arg->client_packs_len = client_packs_len;

  MC_LOGI("static_server", "registry fetch: connecting to %s:%d as %s (client packs %zu B)", g_fetch.host,
          g_fetch.port, g_fetch.username ? g_fetch.username : "FlayerBot", client_packs_len);

  pthread_t tid;
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
  int rc = pthread_create(&tid, &attr, registry_fetch_thread, arg);
  pthread_attr_destroy(&attr);
  if (rc != 0) {
    MC_LOGE("static_server", "registry fetch thread failed");
    free(arg->client_packs);
    free(arg);
    return -1;
  }
  return 0;
}

static int load_from_remote_capture(const mc_registry_capture_result *cap) {
  g_steps = cap->steps;
  g_step_count = cap->step_count;
  g_owns_steps = 1;
  g_send_reset_chat_before_finish = 0;

  g_registry_count = 0;
  for (size_t i = 0; i < g_step_count; i++) {
    if (g_steps[i].kind == MC_REG_SYNC_REGISTRY) g_registry_count++;
  }

  g_registries = (lc_registry_data *)calloc(g_registry_count, sizeof(lc_registry_data));
  if (!g_registries) return -1;

  size_t ri = 0;
  for (size_t i = 0; i < g_step_count; i++) {
    if (g_steps[i].kind != MC_REG_SYNC_REGISTRY) continue;
    if (lc_parse_registry_data(g_steps[i].data, g_steps[i].len, &g_registries[ri]) != LC_OK) {
      MC_LOGE("static_server", "failed to parse fetched registry %s", g_steps[i].label);
      return -1;
    }
    ri++;
  }

  memset(&g_tags, 0, sizeof g_tags);
  for (size_t i = 0; i < g_step_count; i++) {
    if (g_steps[i].kind != MC_REG_SYNC_TAGS) continue;
    if (lc_parse_update_tags(g_steps[i].data, g_steps[i].len, &g_tags) != LC_OK) {
      MC_LOGE("static_server", "failed to parse fetched update_tags");
      return -1;
    }
    g_tags_loaded = 1;
    break;
  }
  if (!g_tags_loaded) {
    MC_LOGE("static_server", "fetched registry set missing update_tags");
    return -1;
  }
  return 0;
}

static int load_from_embedded(void) {
  if (build_steps_from_embedded() != 0) return -1;

  g_registry_count = mc_static_registry_blob_count;
  g_registries = (lc_registry_data *)calloc(g_registry_count, sizeof(lc_registry_data));
  if (!g_registries) return -1;

  for (size_t i = 0; i < g_registry_count; i++) {
    const mc_static_blob *blob = &mc_static_registry_blobs[i];
    if (lc_parse_registry_data(blob->data, blob->len, &g_registries[i]) != LC_OK) {
      MC_LOGE("static_server", "failed to parse registry %s", blob->label);
      return -1;
    }
  }

  memset(&g_tags, 0, sizeof g_tags);
  if (lc_parse_update_tags(mc_static_tags.data, mc_static_tags.len, &g_tags) != LC_OK) {
    MC_LOGE("static_server", "failed to parse update_tags");
    return -1;
  }
  g_tags_loaded = 1;
  return 0;
}

static int ensure_registries_loaded(const uint8_t *client_packs, size_t client_packs_len) {
  pthread_mutex_lock(&g_load_mutex);
  while (g_load_state == REG_STATE_LOADING) pthread_cond_wait(&g_load_cond, &g_load_mutex);

  if (g_load_state == REG_STATE_READY || g_load_state == REG_STATE_CONFIG_READY) {
    int ok = !g_fetch_enabled || packs_key_matches(client_packs, client_packs_len);
    if (ok) {
      pthread_mutex_unlock(&g_load_mutex);
      return 0;
    }
    while (g_load_state == REG_STATE_CONFIG_READY) pthread_cond_wait(&g_load_cond, &g_load_mutex);
    if (g_load_state == REG_STATE_READY && packs_key_matches(client_packs, client_packs_len)) {
      pthread_mutex_unlock(&g_load_mutex);
      return 0;
    }
    clear_loaded_data();
    g_load_state = REG_STATE_EMPTY;
  }
  if (g_load_state == REG_STATE_FAILED) g_load_state = REG_STATE_EMPTY;

  if (!g_fetch_enabled) {
    clear_loaded_data();
    g_load_state = REG_STATE_LOADING;
    pthread_mutex_unlock(&g_load_mutex);
    if (load_from_embedded() != 0) {
      pthread_mutex_lock(&g_load_mutex);
      g_load_state = REG_STATE_FAILED;
      pthread_cond_broadcast(&g_load_cond);
      pthread_mutex_unlock(&g_load_mutex);
      return -1;
    }
    pthread_mutex_lock(&g_load_mutex);
    g_load_state = REG_STATE_READY;
    pthread_cond_broadcast(&g_load_cond);
    pthread_mutex_unlock(&g_load_mutex);
    return 0;
  }

  clear_loaded_data();
  g_load_state = REG_STATE_LOADING;
  pthread_mutex_unlock(&g_load_mutex);

  if (start_remote_fetch(client_packs, client_packs_len) != 0) {
    pthread_mutex_lock(&g_load_mutex);
    g_load_state = REG_STATE_FAILED;
    pthread_cond_broadcast(&g_load_cond);
    pthread_mutex_unlock(&g_load_mutex);
    return -1;
  }

  pthread_mutex_lock(&g_load_mutex);
  while (g_load_state == REG_STATE_LOADING) pthread_cond_wait(&g_load_cond, &g_load_mutex);
  int rc = (g_load_state == REG_STATE_CONFIG_READY || g_load_state == REG_STATE_READY) ? 0 : -1;
  pthread_mutex_unlock(&g_load_mutex);
  return rc;
}

static const mc_reg_sync_step *find_cached_step(mc_reg_sync_kind kind, int32_t pkt_id) {
  if ((g_load_state != REG_STATE_READY && g_load_state != REG_STATE_CONFIG_READY) || !g_steps) return NULL;
  for (size_t i = 0; i < g_step_count; i++) {
    const mc_reg_sync_step *step = &g_steps[i];
    if (step->kind != kind) continue;
    if (kind == MC_REG_SYNC_PLAY && step->pkt_id != pkt_id) continue;
    return step;
  }
  return NULL;
}

const uint8_t *mc_static_cached_select_known_packs(size_t *len) {
  const mc_reg_sync_step *step = find_cached_step(MC_REG_SYNC_SELECT_KNOWN_PACKS, 0);
  if (!step) return NULL;
  if (len) *len = step->len;
  return step->data;
}

const uint8_t *mc_static_cached_play_payload(int32_t pkt_id, size_t *len) {
  const mc_reg_sync_step *step = find_cached_step(MC_REG_SYNC_PLAY, pkt_id);
  if (!step) return NULL;
  if (len) *len = step->len;
  return step->data;
}

int mc_static_send_cached_recipe_burst(int fd) {
  static const int32_t order[] = {
      MC_PKT_PLAY_UPDATE_RECIPES,
      MC_PKT_PLAY_RECIPE_BOOK_SETTINGS,
      MC_PKT_PLAY_RECIPE_BOOK_ADD,
  };
  for (size_t i = 0; i < sizeof order / sizeof order[0]; i++) {
    size_t len = 0;
    const uint8_t *payload = mc_static_cached_play_payload(order[i], &len);
    if (!payload) continue;
    if (mc_send_frame(fd, order[i], payload, len) != 0) return -1;
  }
  return 0;
}

void mc_static_wait_play_cache(void) {
  if (!g_fetch_enabled) return;
  pthread_mutex_lock(&g_load_mutex);
  while (g_load_state == REG_STATE_CONFIG_READY) pthread_cond_wait(&g_load_cond, &g_load_mutex);
  pthread_mutex_unlock(&g_load_mutex);
}

int mc_static_fill_join_login(lc_play_login *login, int32_t entity_id) {
  if (!login || !g_cached_login_valid) return -1;
  memset(login, 0, sizeof *login);
  login->entity_id = entity_id;
  login->hardcore = g_login_hardcore;
  login->world_names = (const char **)g_login_world_names;
  login->world_name_count = g_login_world_name_count;
  login->max_players = g_login_max_players;
  login->view_distance = g_login_view_distance;
  login->simulation_distance = g_login_simulation_distance;
  login->reduced_debug_info = g_login_reduced_debug_info;
  login->enable_respawn_screen = g_login_enable_respawn_screen;
  login->do_limited_crafting = g_login_do_limited_crafting;
  login->enforces_secure_chat = g_login_enforces_secure_chat;
  login->world_state.dimension = g_login_world_state.dimension;
  login->world_state.hashed_seed = g_login_world_state.hashed_seed;
  login->world_state.gamemode = g_login_world_state.gamemode;
  login->world_state.previous_gamemode = g_login_world_state.previous_gamemode;
  login->world_state.is_debug = g_login_world_state.is_debug;
  login->world_state.is_flat = g_login_world_state.is_flat;
  login->world_state.has_death = 0;
  login->world_state.portal_cooldown = g_login_world_state.portal_cooldown;
  login->world_state.sea_level = g_login_world_state.sea_level;
  login->world_state.name = g_login_world_state.name ? strdup(g_login_world_state.name) : NULL;
  return login->world_state.name || !g_login_world_state.name ? 0 : -1;
}

int mc_static_fill_join_position(lc_position *pos, int32_t teleport_id) {
  if (!pos || !g_cached_position_valid) return -1;
  *pos = g_cached_position;
  pos->teleport_id = teleport_id;
  return 0;
}

int mc_static_registries_init(void) {
  mc_static_registries_free();
  return 0;
}

void mc_static_registries_free(void) {
  pthread_mutex_lock(&g_load_mutex);
  clear_loaded_data();
  g_load_state = REG_STATE_EMPTY;
  pthread_cond_broadcast(&g_load_cond);
  pthread_mutex_unlock(&g_load_mutex);
}

int mc_static_send_registry_sync(int fd, const uint8_t *client_known_packs, size_t client_known_packs_len) {
  if (!client_known_packs && client_known_packs_len > 0) return -1;
  if (ensure_registries_loaded(client_known_packs, client_known_packs_len) != 0) return -1;
  if (!g_registries || !g_tags_loaded || !g_steps) return -1;

  int sent_reset_chat = 0;
  for (size_t i = 0; i < g_step_count; i++) {
    const mc_reg_sync_step *step = &g_steps[i];
    switch (step->kind) {
    case MC_REG_SYNC_REGISTRY:
      if (send_registry_payload(fd, step->data, step->len) != 0) return -1;
      break;
    case MC_REG_SYNC_TAGS:
      if (send_tags_payload(fd, step->data, step->len) != 0) return -1;
      break;
    case MC_REG_SYNC_RESET_CHAT:
      if (step->len) {
        if (mc_send_frame(fd, MC_PKT_CFG_RESET_CHAT, step->data, step->len) != 0) return -1;
      } else if (send_reset_chat(fd) != 0) {
        return -1;
      }
      sent_reset_chat = 1;
      break;
    default:
      break;
    }
  }
  if (g_send_reset_chat_before_finish && !sent_reset_chat && send_reset_chat(fd) != 0) return -1;
  return mc_send_frame(fd, MC_PKT_CFG_FINISH, NULL, 0);
}

size_t mc_static_registry_count(void) {
  pthread_mutex_lock(&g_load_mutex);
  int ready = g_load_state == REG_STATE_READY || g_load_state == REG_STATE_CONFIG_READY;
  size_t n = ready ? g_registry_count : 0;
  pthread_mutex_unlock(&g_load_mutex);
  return n;
}

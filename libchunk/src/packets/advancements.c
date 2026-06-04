#include "../internal.h"

static lc_status skip_opt_string(lc_buf *b) {
  uint8_t present;
  if (lc_buf_read_bool(b, &present) != LC_OK) return LC_ERR_TRUNCATED;
  if (!present) return LC_OK;
  char *s = NULL;
  lc_status st = lc_buf_read_string(b, &s);
  free(s);
  return st;
}

static lc_status skip_requirements(lc_buf *b) {
  int32_t outer;
  if (lc_buf_read_varint(b, &outer) != LC_OK) return LC_ERR_TRUNCATED;
  if (outer < 0) return LC_ERR_INVALID;
  for (int32_t i = 0; i < outer; i++) {
    int32_t inner;
    if (lc_buf_read_varint(b, &inner) != LC_OK) return LC_ERR_TRUNCATED;
    if (inner < 0) return LC_ERR_INVALID;
    for (int32_t j = 0; j < inner; j++) {
      char *s = NULL;
      if (lc_buf_read_string(b, &s) != LC_OK) return LC_ERR_TRUNCATED;
      free(s);
    }
  }
  return LC_OK;
}

static lc_status skip_display_data(lc_buf *b) {
  uint8_t present;
  if (lc_buf_read_bool(b, &present) != LC_OK) return LC_ERR_TRUNCATED;
  if (!present) return LC_OK;
  if (lc_nbt_skip_anonymous(b) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_nbt_skip_anonymous(b) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_slot_skip(b) != LC_OK) return LC_ERR_TRUNCATED;
  int32_t frame;
  if (lc_buf_read_varint(b, &frame) != LC_OK) return LC_ERR_TRUNCATED;
  static const lc_bitfield_spec flag_spec[] = {{29, 0}, {1, 0}, {1, 0}, {1, 0}};
  int32_t flags[4];
  if (lc_buf_read_bitfield(b, flag_spec, 4, flags) != LC_OK) return LC_ERR_TRUNCATED;
  if (flags[3]) {
    char *tex = NULL;
    if (lc_buf_read_string(b, &tex) != LC_OK) return LC_ERR_TRUNCATED;
    free(tex);
  }
  float x, y;
  if (lc_buf_read_f32_be(b, &x) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_f32_be(b, &y) != LC_OK) return LC_ERR_TRUNCATED;
  return LC_OK;
}

static lc_status skip_advancement_value(lc_buf *b) {
  if (skip_opt_string(b) != LC_OK) return LC_ERR_TRUNCATED;
  if (skip_display_data(b) != LC_OK) return LC_ERR_TRUNCATED;
  if (skip_requirements(b) != LC_OK) return LC_ERR_TRUNCATED;
  uint8_t telemetry;
  return lc_buf_read_bool(b, &telemetry);
}

static lc_status skip_progress_entry(lc_buf *b) {
  char *criterion = NULL;
  if (lc_buf_read_string(b, &criterion) != LC_OK) return LC_ERR_TRUNCATED;
  free(criterion);
  uint8_t present;
  if (lc_buf_read_bool(b, &present) != LC_OK) return LC_ERR_TRUNCATED;
  if (present) {
    int64_t progress;
    if (lc_buf_read_i64_le(b, &progress) != LC_OK) return LC_ERR_TRUNCATED;
  }
  return LC_OK;
}

int lc_decode_advancements(const uint8_t *payload, size_t payload_len, char *out, size_t out_sz) {
  lc_buf b;
  lc_buf_init(&b, payload, payload_len);
  uint8_t reset;
  if (lc_buf_read_bool(&b, &reset) != LC_OK) return -1;

  int32_t adv_count;
  if (lc_buf_read_varint(&b, &adv_count) != LC_OK) return -1;
  if (adv_count < 0) return -1;

  char first_key[80] = "";
  char second_key[80] = "";
  for (int32_t i = 0; i < adv_count; i++) {
    char *key = NULL;
    if (lc_buf_read_string(&b, &key) != LC_OK) return -1;
    if (i == 0 && key) snprintf(first_key, sizeof first_key, "%s", key);
    if (i == 1 && key) snprintf(second_key, sizeof second_key, "%s", key);
    free(key);
    if (skip_advancement_value(&b) != LC_OK) return -1;
  }

  int32_t id_count;
  if (lc_buf_read_varint(&b, &id_count) != LC_OK) return -1;
  if (id_count < 0) return -1;
  for (int32_t i = 0; i < id_count; i++) {
    char *id = NULL;
    if (lc_buf_read_string(&b, &id) != LC_OK) return -1;
    free(id);
  }

  int32_t progress_count;
  if (lc_buf_read_varint(&b, &progress_count) != LC_OK) return -1;
  if (progress_count < 0) return -1;
  for (int32_t i = 0; i < progress_count; i++) {
    char *key = NULL;
    if (lc_buf_read_string(&b, &key) != LC_OK) return -1;
    free(key);
    int32_t entries;
    if (lc_buf_read_varint(&b, &entries) != LC_OK) return -1;
    if (entries < 0) return -1;
    for (int32_t j = 0; j < entries; j++) {
      if (skip_progress_entry(&b) != LC_OK) return -1;
    }
  }

  uint8_t show;
  if (lc_buf_read_bool(&b, &show) != LC_OK) return -1;

  int w = lc_snprintf(out, out_sz,
                      "advancements{reset=%s,advancements=%d,identifiers=%d,progress=%d,"
                      "showAdvancements=%s",
                      reset ? "true" : "false", adv_count, id_count, progress_count,
                      show ? "true" : "false");
  if (w <= 0 || (size_t)w >= out_sz) return -1;

  if (first_key[0]) {
    if (second_key[0]) {
      w = lc_appendf(out, out_sz, w, ",keys=[%s,%s", first_key, second_key);
      if (adv_count > 2) w = lc_appendf(out, out_sz, w, ",...");
    } else {
      w = lc_appendf(out, out_sz, w, ",keys=[%s]", first_key);
    }
    if (w > 0 && second_key[0] && adv_count > 1) w = lc_appendf(out, out_sz, w, "]");
  }

  if (w > 0 && lc_buf_remaining(&b) > 0) {
    w = lc_appendf(out, out_sz, w, ",bytesRemaining=%zu}", lc_buf_remaining(&b));
  } else if (w > 0) {
    w = lc_appendf(out, out_sz, w, "}");
  }

  return w > 0 && (size_t)w < out_sz ? 1 : -1;
}

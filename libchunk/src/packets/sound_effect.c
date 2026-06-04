#include "../internal.h"

static const char *SOUND_SOURCE_NAMES[] = {
    "master", "music", "records", "weather", "block", "hostile",
    "neutral", "player", "ambient", "voice", "ui",
};

const char *lc_sound_source_name(int32_t source) {
  if (source >= 0 && source < (int32_t)(sizeof(SOUND_SOURCE_NAMES) / sizeof(SOUND_SOURCE_NAMES[0])))
    return SOUND_SOURCE_NAMES[source];
  return "?";
}

void lc_sound_event_ref_free(lc_sound_event_ref *p) {
  if (!p) return;
  free(p->id);
  p->id = NULL;
  p->has_fixed_range = 0;
  p->fixed_range = 0;
}

static lc_status lc_buf_read_sound_event_ref(lc_buf *b, lc_sound_event_ref *out) {
  memset(out, 0, sizeof(*out));
  int32_t tag;
  if (lc_buf_read_varint(b, &tag) != LC_OK) return LC_ERR_TRUNCATED;
  if (tag == 0) {
    if (lc_buf_read_string(b, &out->id) != LC_OK) return LC_ERR_TRUNCATED;
    uint8_t has_range;
    if (lc_buf_read_bool(b, &has_range) != LC_OK) return LC_ERR_TRUNCATED;
    out->has_fixed_range = has_range;
    if (has_range) {
      if (lc_buf_read_f32_le(b, &out->fixed_range) != LC_OK) return LC_ERR_TRUNCATED;
    }
    return LC_OK;
  }
  out->id = (char *)malloc(24);
  if (!out->id) return LC_ERR_OOM;
  lc_snprintf(out->id, 24, "#%d", tag - 1);
  return LC_OK;
}

lc_status lc_parse_sound_effect(const uint8_t *data, size_t len, lc_sound_effect *out) {
  memset(out, 0, sizeof(*out));
  lc_buf b;
  lc_buf_init(&b, data, len);
  if (lc_buf_read_sound_event_ref(&b, &out->sound) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_varint(&b, &out->source) != LC_OK) goto fail;
  if (lc_buf_read_i32_be(&b, &out->x) != LC_OK) goto fail;
  if (lc_buf_read_i32_be(&b, &out->y) != LC_OK) goto fail;
  if (lc_buf_read_i32_be(&b, &out->z) != LC_OK) goto fail;
  if (lc_buf_read_f32_le(&b, &out->volume) != LC_OK) goto fail;
  if (lc_buf_read_f32_le(&b, &out->pitch) != LC_OK) goto fail;
  if (lc_buf_read_i64_le(&b, &out->seed) != LC_OK) goto fail;
  return LC_OK;

fail:
  lc_sound_event_ref_free(&out->sound);
  memset(out, 0, sizeof(*out));
  return LC_ERR_TRUNCATED;
}

void lc_sound_effect_free(lc_sound_effect *p) {
  if (!p) return;
  lc_sound_event_ref_free(&p->sound);
  memset(p, 0, sizeof(*p));
}

static int append_sound_ref(char *buf, size_t buflen, int w, const lc_sound_event_ref *sound) {
  if (!sound || !sound->id) return w;
  w = lc_appendf(buf, buflen, w, "sound=%s", sound->id);
  if (sound->has_fixed_range)
    w = lc_appendf(buf, buflen, w, ",range=%.3f", sound->fixed_range);
  return w;
}

int lc_sound_effect_to_string(const lc_sound_effect *p, char *buf, size_t buflen) {
  if (!p || !buf || buflen == 0) return 0;
  double wx = (double)p->x / 8.0;
  double wy = (double)p->y / 8.0;
  double wz = (double)p->z / 8.0;
  int w = lc_snprintf(buf, buflen, "sound_effect{");
  w = append_sound_ref(buf, buflen, w, &p->sound);
  w = lc_appendf(buf, buflen, w, ",source=%s,pos=(%.3f,%.3f,%.3f),volume=%.2f,pitch=%.2f,seed=%lld}",
                 lc_sound_source_name(p->source), wx, wy, wz, p->volume, p->pitch,
                 (long long)p->seed);
  return w;
}

lc_status lc_parse_entity_sound_effect(const uint8_t *data, size_t len, lc_entity_sound_effect *out) {
  memset(out, 0, sizeof(*out));
  lc_buf b;
  lc_buf_init(&b, data, len);
  if (lc_buf_read_sound_event_ref(&b, &out->sound) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_varint(&b, &out->source) != LC_OK) goto fail;
  if (lc_buf_read_varint(&b, &out->entity_id) != LC_OK) goto fail;
  if (lc_buf_read_f32_le(&b, &out->volume) != LC_OK) goto fail;
  if (lc_buf_read_f32_le(&b, &out->pitch) != LC_OK) goto fail;
  if (lc_buf_read_i64_le(&b, &out->seed) != LC_OK) goto fail;
  return LC_OK;

fail:
  lc_sound_event_ref_free(&out->sound);
  memset(out, 0, sizeof(*out));
  return LC_ERR_TRUNCATED;
}

void lc_entity_sound_effect_free(lc_entity_sound_effect *p) {
  if (!p) return;
  lc_sound_event_ref_free(&p->sound);
  memset(p, 0, sizeof(*p));
}

int lc_entity_sound_effect_to_string(const lc_entity_sound_effect *p, char *buf, size_t buflen) {
  if (!p || !buf || buflen == 0) return 0;
  int w = lc_snprintf(buf, buflen, "entity_sound_effect{");
  w = append_sound_ref(buf, buflen, w, &p->sound);
  w = lc_appendf(buf, buflen, w, ",source=%s,entityId=%d,volume=%.2f,pitch=%.2f,seed=%lld}",
                 lc_sound_source_name(p->source), p->entity_id, p->volume, p->pitch,
                 (long long)p->seed);
  return w;
}

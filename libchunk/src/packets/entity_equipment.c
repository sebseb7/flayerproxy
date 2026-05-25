#include "../internal.h"

lc_status lc_parse_entity_equipment(const uint8_t *data, size_t len, lc_entity_equipment *out) {
  memset(out, 0, sizeof(*out));
  lc_buf b;
  lc_buf_init(&b, data, len);
  if (lc_buf_read_varint(&b, &out->entity_id) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_read_top_bit_array(&b, &out->equipments) != LC_OK) return LC_ERR_TRUNCATED;
  return LC_OK;
}

void lc_entity_equipment_free(lc_entity_equipment *p) {
  lc_equipment_arr_free(&p->equipments);
  memset(p, 0, sizeof(*p));
}

int lc_entity_equipment_to_string(const lc_entity_equipment *p, char *buf, size_t buflen) {
  if (!p || !buf || buflen == 0) return 0;
  int w = lc_snprintf(buf, buflen, "entity_equipment{id=%d,slots=[", p->entity_id);
  for (size_t i = 0; i < p->equipments.count; i++) {
    const lc_equipment *eq = &p->equipments.items[i];
    w = lc_appendf(buf, buflen, w, "%s{slot=%d,count=%d,itemId=%d,extra=%zu b}", i ? "," : "",
                   (int)eq->slot, eq->item_count, eq->item_id, eq->item_extra.len);
  }
  return lc_appendf(buf, buflen, w, "]}");
}

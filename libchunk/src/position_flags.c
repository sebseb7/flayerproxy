#include "internal.h"

/** 1 << shift per net.minecraft.world.entity.Relative (1.21.10). */
static const struct {
  uint32_t mask;
  const char *name;
} LC_POSITION_REL[] = {
    {1u << 0, "X"},
    {1u << 1, "Y"},
    {1u << 2, "Z"},
    {1u << 3, "Y_ROT"},
    {1u << 4, "X_ROT"},
    {1u << 5, "DELTA_X"},
    {1u << 6, "DELTA_Y"},
    {1u << 7, "DELTA_Z"},
    {1u << 8, "ROTATE_DELTA"},
};

int lc_position_relatives_to_string(uint32_t flags, char *buf, size_t buflen) {
  if (!buf || buflen == 0) return 0;
  if (flags == 0) return lc_snprintf(buf, buflen, "rel=absolute");

  size_t n = sizeof(LC_POSITION_REL) / sizeof(LC_POSITION_REL[0]);
  char *p = buf;
  size_t left = buflen;
  int wrote = lc_snprintf(p, left, "rel=");
  if (wrote < 0 || (size_t)wrote >= left) return 0;
  p += wrote;
  left -= (size_t)wrote;

  int first = 1;
  for (size_t i = 0; i < n; i++) {
    if ((flags & LC_POSITION_REL[i].mask) == 0) continue;
    int part = lc_snprintf(p, left, "%s%s", first ? "" : "|", LC_POSITION_REL[i].name);
    if (part < 0 || (size_t)part >= left) return 0;
    p += part;
    left -= (size_t)part;
    first = 0;
  }

  uint32_t known = 0;
  for (size_t i = 0; i < n; i++) known |= LC_POSITION_REL[i].mask;
  if ((flags & ~known) != 0) {
    int part = lc_snprintf(p, left, "%s0x%x?", first ? "" : "|", flags & ~known);
    if (part < 0 || (size_t)part >= left) return 0;
  }
  return 1;
}

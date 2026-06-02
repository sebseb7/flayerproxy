#include "mc_registry_join_template.h"

#include <stdlib.h>
#include <string.h>

void mc_registry_join_template_clear(mc_registry_join_template *t) {
  if (!t) return;
  if (t->login_world_names) {
    for (size_t i = 0; i < t->login_world_name_count; i++) free(t->login_world_names[i]);
    free(t->login_world_names);
  }
  lc_spawn_info_free(&t->login_world_state);
  free(t->spawn_dimension);
  memset(t, 0, sizeof *t);
}

void mc_registry_join_template_free(mc_registry_join_template *t) {
  mc_registry_join_template_clear(t);
}

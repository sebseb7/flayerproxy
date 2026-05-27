#ifndef MC_STATIC_REGISTRIES_H
#define MC_STATIC_REGISTRIES_H

/** Parse embedded reference blobs once; serialize via lc_write_* when sending. */
int mc_static_registries_init(void);
void mc_static_registries_free(void);

/** After client select_known_packs: registry_data (×N), update_tags, finish_configuration. */
int mc_static_send_registry_sync(int fd);

#endif

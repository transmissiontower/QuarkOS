#ifndef USER_PATH_H
#define USER_PATH_H
#include "lib/stdint.h"
int qk_path_resolve_ext(uint16_t start_cluster, const char *path, uint16_t *out_dir_cluster,
                        char *out_name);
int qk_path_resolve(const char *path, uint16_t *out_dir_cluster, char *out_name);
#endif

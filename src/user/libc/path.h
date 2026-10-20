#ifndef USER_PATH_H
#define USER_PATH_H
#include "stdint.h"
int qk_path_resolve_ext(uint32_t start_cluster, const char *path, uint32_t *out_dir_cluster,
                        char *out_name);
int qk_path_resolve(const char *path, uint32_t *out_dir_cluster, char *out_name);
#endif

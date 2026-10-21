#ifndef APP_REGISTRY_H
#define APP_REGISTRY_H

#include "stdint.h"

int app_registry_resolve(int storage_pid, const char *kind, const char *key, char *path,
                         uint32_t path_capacity);

#endif

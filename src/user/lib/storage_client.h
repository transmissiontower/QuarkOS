#ifndef STORAGE_CLIENT_H
#define STORAGE_CLIENT_H
#include "lib/stdint.h"
int storage_send_file(int storage_pid, uint16_t dir_cluster, const char *filename,
                      const uint8_t *data, uint32_t size);
#endif

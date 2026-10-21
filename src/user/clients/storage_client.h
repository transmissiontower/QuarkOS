#ifndef STORAGE_CLIENT_H
#define STORAGE_CLIENT_H
#include "stdint.h"
int storage_send_file(int storage_pid, uint32_t dir_cluster, const char *filename,
                      const uint8_t *data, uint32_t size, int disk_idx);
void *storage_read_file(int storage_pid, const char *path, uint32_t *size_out);
#endif

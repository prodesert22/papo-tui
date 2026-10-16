#ifndef PAPO_READ_STORAGE_H
#define PAPO_READ_STORAGE_H
#include "config/read_state.h"
#define STATE_BYTES (4U * 1024U * 1024U)
#define CONTEXT_LIMIT 64U
typedef struct read_storage read_storage;
read_storage *read_storage_open(void);
void read_storage_close(read_storage *storage);
bool read_storage_lock(read_storage *storage);
json_t *read_storage_load(read_storage *storage);
bool read_storage_write(read_storage *storage, json_t *root);
bool read_state_validate(json_t *root);
#endif

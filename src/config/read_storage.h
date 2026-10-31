#ifndef PAPO_READ_STORAGE_H
#define PAPO_READ_STORAGE_H
#include "config/read_state.h"
#define STATE_BYTES (4U * 1024U * 1024U)
#define CONTEXT_LIMIT 64U
typedef struct read_storage read_storage;
/* Private state file <stem>.json with <stem>.lock, beside the read cursors.
 * Roots are {"version":1,"contexts":[...]}; validate rejects any other shape,
 * and writes drop the oldest contexts to stay within STATE_BYTES. stem is
 * borrowed and must stay valid while the storage is open. */
read_storage *read_storage_open_named(const char *stem, bool (*validate)(json_t *root));
/* The read cursors (read-cursors.json, read_state_validate). */
read_storage *read_storage_open(void);
void read_storage_close(read_storage *storage);
bool read_storage_lock(read_storage *storage);
json_t *read_storage_load(read_storage *storage);
bool read_storage_write(read_storage *storage, json_t *root);
bool read_state_validate(json_t *root);
#endif

#ifndef PAPO_READ_STATE_H
#define PAPO_READ_STATE_H
#include <jansson.h>
#include <stdbool.h>

#define READ_STATE_LIMIT 1000U

/* Only validated message IDs/timestamps, never content or credentials. Scope
 * is the normalized instance URL plus verified user UUID. Load returns an
 * owned map (empty when unavailable); save borrows its map and merges under
 * a nonblocking file lock. Files are private and atomically replaced. */
json_t *read_state_load(const char *url, const char *user);
bool read_state_save(const char *url, const char *user, const json_t *messages);
#endif

#ifndef PAPO_CHANNEL_STORE_H
#define PAPO_CHANNEL_STORE_H
#include <jansson.h>
#include <stdbool.h>

/* A private GIO file holding one JSON map of channel UUIDs for one verified
 * account and normalized instance, under papo-tui/<folder>. A malformed file,
 * a symlink or a file changed by another writer is never overwritten. No
 * credentials or message contents. All calls on the UI thread. */
typedef bool (*channel_store_valid)(const json_t *value);
typedef struct channel_store channel_store;
channel_store *channel_store_open(const char *url, const char *user, const char *folder,
                                  channel_store_valid valid);
void channel_store_close(channel_store *store);
/* Borrowed map. In-memory changes are kept until the next save. */
json_t *channel_store_channels(const channel_store *store);
/* Whether another writer replaced or removed the file since it was read or
 * saved here; a symlink never counts as a change. */
bool channel_store_changed(const channel_store *store);
/* Saves a borrowed replacement map; on failure the store keeps its map. */
bool channel_store_save(channel_store *store, json_t *channels);
#endif

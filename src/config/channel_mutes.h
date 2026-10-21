#ifndef PAPO_CHANNEL_MUTES_H
#define PAPO_CHANNEL_MUTES_H
#include <stdbool.h>
#include <stdint.h>

typedef struct channel_mutes channel_mutes;
/* Owns a bounded, private GIO file and JSON map for one verified account and
 * normalized instance. No credentials or message contents. All calls on UI thread. */
channel_mutes *channel_mutes_create(const char *url, const char *user);
void channel_mutes_destroy(channel_mutes *mutes);
bool channel_mutes_active(const channel_mutes *mutes, const char *channel, uint64_t now);
/* Seconds: 0 reactivates, -1 indefinite, otherwise one of the UI durations.
 * A failed save leaves the in-memory preference unchanged. */
bool channel_mutes_set(channel_mutes *mutes, const char *channel, int seconds, uint64_t now);
bool channel_mutes_expire(channel_mutes *mutes, uint64_t now);
#endif

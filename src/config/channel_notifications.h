#ifndef PAPO_CHANNEL_NOTIFICATIONS_H
#define PAPO_CHANNEL_NOTIFICATIONS_H
#include <stdbool.h>

/* Channel notification modes kept only on this device, for channels that do
 * not sync them with the server. Scoped to one verified account and
 * normalized instance; all calls on the UI thread. */
typedef struct channel_notifications channel_notifications;
channel_notifications *channel_notifications_create(const char *url, const char *user);
void channel_notifications_destroy(channel_notifications *local);
/* The device mode of channel, or NULL when it follows the server. */
const char *channel_notifications_mode(const channel_notifications *local, const char *channel);
/* Reads the file again when another instance on this device changed it. */
bool channel_notifications_refresh(channel_notifications *local);
/* "all", "only_mentions" or "off" keeps the mode on this device; NULL syncs
 * the channel with the server again. A failed save changes nothing. */
bool channel_notifications_set(channel_notifications *local, const char *channel, const char *mode);
#endif

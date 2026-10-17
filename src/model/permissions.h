#ifndef PAPO_PERMISSIONS_H
#define PAPO_PERMISSIONS_H
#include <jansson.h>
#include <stdbool.h>
#include <stdint.h>

typedef enum { PERMISSION_UNKNOWN, PERMISSION_DENIED, PERMISSION_ALLOWED } permission_decision;
typedef enum {
    PERMISSION_MANAGE_SERVER,
    PERMISSION_MANAGE_CHANNELS,
    PERMISSION_MANAGE_ROLES,
    PERMISSION_BAN_MEMBERS,
    PERMISSION_PIN_MESSAGE,
    PERMISSION_EVERYONE_MESSAGE,
    PERMISSION_SEND_ATTACHMENT,
    PERMISSION_ROLE_COUNT
} role_permission;
typedef enum {
    PERMISSION_READ_CHANNEL,
    PERMISSION_SEND_MESSAGES,
    PERMISSION_DELETE_MESSAGES,
    PERMISSION_CONNECT_VOICE,
    PERMISSION_CHANNEL_COUNT
} channel_permission;
typedef enum { PERMISSIONS_SERVER, PERMISSIONS_ROLES, PERMISSIONS_CHANNELS } permission_source;
typedef struct permissions permissions;
/* Owns bounded snapshots; all operations are on the event-loop thread. */
permissions *permissions_create(void);
void permissions_destroy(permissions *p);
void permissions_reset(permissions *p);
/* Missing/invalid data invalidates only that source. Snapshots are copied. */
bool permissions_update(permissions *p, permission_source source, const json_t *body);
bool permissions_identity(permissions *p, const json_t *body, uint64_t event_serial);
bool permissions_event(permissions *p, const json_t *event);
void permissions_invalidate(permissions *p, permission_source source);
void permissions_dm(permissions *p, const char *id, bool available);
uint64_t permissions_revision(const permissions *p);
uint64_t permissions_channel_revision(const permissions *p);
uint64_t permissions_event_serial(const permissions *p);
permission_decision permissions_role(const permissions *p, role_permission permission);
permission_decision permissions_channel(const permissions *p, const char *id,
                                        channel_permission permission);
permission_decision permissions_message(const permissions *p, const json_t *message, bool edit);
/* Borrowed full channel metadata, including channels hidden from the user. */
const json_t *permissions_channels(const permissions *p);
#endif

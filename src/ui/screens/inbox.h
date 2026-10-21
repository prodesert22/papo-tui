#include <stdint.h>
#ifndef PAPO_R8_MODAL_H
#define PAPO_R8_MODAL_H
#include "config/profiles.h"
#include "model/conversation/history.h"
#include "ui/input/text.h"
typedef struct r8_modal r8_modal;
typedef enum { R8_NOTIFICATIONS, R8_USERS, R8_PINNED, R8_SEARCH } r8_modal_kind;
r8_modal_kind r8_modal_type(const r8_modal *m);
r8_modal *r8_modal_create(history *h, auth_client *auth, profile_config *config);
void r8_modal_destroy(r8_modal *m);
void r8_modal_open(r8_modal *m, r8_modal_kind kind);
bool r8_modal_is_open(const r8_modal *m);
void r8_modal_close(r8_modal *m);
bool r8_modal_key(r8_modal *m, int type, uint32_t key);
/* Returned window is borrowed until the next draw or close. */
WINDOW *r8_modal_draw(r8_modal *m);
/* Position the virtual cursor for notification/conversation-selector search. */
bool r8_modal_cursor(r8_modal *m);
/* Animated location view preserves the originating list/details until ready. */
bool r8_modal_locating(const r8_modal *m);
bool r8_modal_poll(r8_modal *m);
void r8_modal_location_started(r8_modal *m);
void r8_modal_location_error(r8_modal *m, const char *error);
void r8_modal_location_complete(r8_modal *m, const char *message);
/* Destination selected explicitly in the central; returned UUID is borrowed. */
const char *r8_modal_destination(const r8_modal *m);
const char *r8_modal_destination_message(const r8_modal *m);
const char *r8_modal_destination_time(const r8_modal *m);
void r8_modal_destination_clear(r8_modal *m);
const char *r8_presence_label(const char *state);
#endif

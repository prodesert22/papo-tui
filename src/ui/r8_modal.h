#ifndef PAPO_R8_MODAL_H
#define PAPO_R8_MODAL_H
#include "model/history.h"
#include "config/profiles.h"
#include "ui/text.h"
typedef struct r8_modal r8_modal;
typedef enum { R8_NOTIFICATIONS, R8_USERS, R8_HELP } r8_modal_kind;
r8_modal *r8_modal_create(history *h,auth_client *auth,profile_config *config);
void r8_modal_destroy(r8_modal *m);
void r8_modal_open(r8_modal *m,r8_modal_kind kind);
bool r8_modal_is_open(const r8_modal *m);
void r8_modal_close(r8_modal *m);
bool r8_modal_key(r8_modal *m,int type,wint_t key);
void r8_modal_draw(r8_modal *m);
/* Destination selected explicitly in the central; returned UUID is borrowed. */
const char *r8_modal_destination(const r8_modal *m);
void r8_modal_destination_clear(r8_modal *m);
const char *r8_presence_label(const char *state);
#endif

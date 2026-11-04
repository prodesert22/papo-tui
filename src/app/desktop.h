#ifndef PAPO_DESKTOP_H
#define PAPO_DESKTOP_H
#include <stdbool.h>
typedef struct desktop_notifier desktop_notifier;
/* Owns its asynchronous GIO context. Strings are copied; never retains auth
 * or model pointers. Keys belong to an instance/session generation. */
desktop_notifier *desktop_create(void);
void desktop_destroy(desktop_notifier *n);
/* target is opaque text (at most DESKTOP_TARGET_BYTES - 1 bytes) returned by
 * desktop_take_activation when the user clicks that notification. */
#define DESKTOP_TARGET_BYTES 128
void desktop_queue(desktop_notifier *n, const char *key, const char *title, const char *body,
                   const char *target);
/* Pops one clicked notification: its key (80 bytes) and target. */
bool desktop_take_activation(desktop_notifier *n, char key[80], char target[DESKTOP_TARGET_BYTES]);
void desktop_cancel(desktop_notifier *n, const char *key);
bool desktop_poll(desktop_notifier *n);
const char *desktop_notice(const desktop_notifier *n);
#endif

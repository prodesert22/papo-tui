#ifndef PAPO_DESKTOP_H
#define PAPO_DESKTOP_H
#include <stdbool.h>
typedef struct desktop_notifier desktop_notifier;
/* Owns its asynchronous GIO context. Strings are copied; never retains auth
 * or model pointers. Keys belong to an instance/session generation. */
desktop_notifier *desktop_create(void);
void desktop_destroy(desktop_notifier *n);
void desktop_queue(desktop_notifier *n,const char *key,const char *title,const char *body);
void desktop_cancel(desktop_notifier *n,const char *key);
bool desktop_poll(desktop_notifier *n);
const char *desktop_notice(const desktop_notifier *n);
#endif

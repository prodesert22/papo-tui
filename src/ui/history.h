#ifndef PAPO_HISTORY_UI_H
#define PAPO_HISTORY_UI_H
#include "net/auth.h"
#include "app/instances.h"
#include <signal.h>
/* Uses the caller's initialized curses terminal/theme and signal flag.
 * Connections are borrowed; selected transfers a tab index on HISTORY_SWITCH. */
enum { HISTORY_ACCOUNT, HISTORY_EXIT, HISTORY_PROFILES, HISTORY_ADD, HISTORY_SWITCH };
int history_run(auth_client *auth, instance_connections *connections, size_t *selected,
                bool focus_tabs, volatile sig_atomic_t *interrupted);
#endif

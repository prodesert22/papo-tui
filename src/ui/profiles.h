#ifndef PAPO_UI_PROFILES_H
#define PAPO_UI_PROFILES_H

#include "config/profiles.h"
#include <signal.h>

/* Uses the caller's ncurses session and signal flag. Public discovery uses
 * the global request budget. Returns 1 for selection, 0 for cancellation,
 * 2 for application exit, -1 for failure. Selection is copied to output. */
int profiles_manage(profile_config *config, bool allow_local_http,
                    const volatile sig_atomic_t *interrupted, server_profile *output);
/* Centered selector opened from login; additionally returns 3 to open management. */
int profiles_choose(profile_config *config, bool allow_local_http,
                    const volatile sig_atomic_t *interrupted, server_profile *output);

#endif

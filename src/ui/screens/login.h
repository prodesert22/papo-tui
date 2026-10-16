#ifndef PAPO_LOGIN_H
#define PAPO_LOGIN_H

#include <stdbool.h>

int login_run(void);
int login_run_profiles(bool open_profiles, bool allow_local_http);

#endif

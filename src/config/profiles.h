#ifndef PAPO_PROFILES_H
#define PAPO_PROFILES_H

#include "config/app_mode.h"
#include <stdbool.h>
#include <stddef.h>
#include <sys/stat.h>

#define PROFILE_LIMIT 64
#define PROFILE_NAME_BYTES 129
#define PROFILE_URL_BYTES 2049
#define PROFILE_ID_BYTES 33

typedef struct {
    char id[PROFILE_ID_BYTES];
    char name[PROFILE_NAME_BYTES];
    char url[PROFILE_URL_BYTES];
} server_profile;

/* Owns its directory descriptor. Contains public connection metadata only.
 * Mutations become visible in memory only after an atomic save succeeds. */
typedef struct profile_config {
    server_profile profiles[PROFILE_LIMIT];
    size_t count;
    char selected[PROFILE_ID_BYTES];
    double requests_per_second;
    unsigned request_burst;
    bool desktop_notifications, notification_preview;
    app_mode mode;
    int directory;
    bool writable, existed;
    struct stat revision;
#ifdef _WIN32
    char *directory_path; /* Owned; candidates borrow from their original. */
    char storage_revision[65];
#endif
    char message[192];
} profile_config;

bool profiles_set_mode(profile_config *config, app_mode mode);
profile_config *profiles_create(void);
void profiles_destroy(profile_config *config);
bool profiles_reload(profile_config *config);
const server_profile *profiles_find(const profile_config *config, const char *id);
bool profiles_update(profile_config *config, const char *id, const char *name, const char *url,
                     bool allow_local_http, char output_id[PROFILE_ID_BYTES]);
bool profiles_remove(profile_config *config, const char *id);
bool profiles_select(profile_config *config, const char *id, bool allow_local_http);
bool profiles_set_rate(profile_config *config, double requests_per_second, unsigned burst);
bool profiles_set_notifications(profile_config *config, bool desktop, bool preview);

#endif

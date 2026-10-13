#ifndef PAPO_PROFILES_H
#define PAPO_PROFILES_H

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
typedef struct {
    server_profile profiles[PROFILE_LIMIT];
    size_t count;
    char selected[PROFILE_ID_BYTES];
    double requests_per_second;
    unsigned request_burst;
    int directory;
    bool writable, existed;
    struct stat revision;
    char message[192];
} profile_config;

profile_config *profiles_create(void);
void profiles_destroy(profile_config *config);
bool profiles_reload(profile_config *config);
const server_profile *profiles_find(const profile_config *config, const char *id);
bool profiles_update(profile_config *config, const char *id, const char *name,
                     const char *url, bool allow_local_http, char output_id[PROFILE_ID_BYTES]);
bool profiles_remove(profile_config *config, const char *id);
bool profiles_select(profile_config *config, const char *id, bool allow_local_http);
bool profiles_set_rate(profile_config *config, double requests_per_second, unsigned burst);

#endif

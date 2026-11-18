#ifndef PAPO_PROFILES_H
#define PAPO_PROFILES_H

#include "config/accessibility.h"
#include "config/app_mode.h"
#include "config/audio.h"
#include "config/keymap.h"
#include "config/player_volume.h"
#include "model/text/translation.h"
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

/* Newest format of the configuration file: the one written, and the last one read. */
#define PROFILE_FILE_VERSION 14
/* How the last load found the configuration file. Only PROFILE_FILE_READY may be saved over. */
typedef enum {
    PROFILE_FILE_READY,       /* Loaded, or absent until the first save. */
    PROFILE_FILE_NEWER,       /* Valid JSON of a format after PROFILE_FILE_VERSION. */
    PROFILE_FILE_INVALID,     /* Damaged, or not a configuration of a known format. */
    PROFILE_FILE_UNREADABLE,  /* Not a private regular file within the size limit. */
    PROFILE_FILE_NO_DIRECTORY /* The private directory could not be opened. */
} profile_file_state;

/* Owns its directory descriptor. Contains public connection metadata only.
 * Mutations become visible in memory only after an atomic save succeeds. */
typedef struct profile_config {
    server_profile profiles[PROFILE_LIMIT];
    size_t count;
    char selected[PROFILE_ID_BYTES];
    double requests_per_second;
    unsigned request_burst;
    /* Switches of this device; the account's own settings must allow it too. */
    bool desktop_notifications, notification_preview, notification_sounds;
    app_mode mode;
    language_choice language;
    bool members_by_status;
    int player_volume;
    /* This device asks GitHub for a newer release by itself, once a day. */
    bool update_check;
    audio_config audio;
    /* Command keys that differ from the defaults (config/keymap.h). */
    keymap_overrides shortcuts;
    accessibility_config accessibility;
    int directory;
    bool writable, existed;
    profile_file_state file_state;
    int file_version; /* Format found in a PROFILE_FILE_NEWER file; 0 otherwise. */
    struct stat revision;
#ifdef _WIN32
    char *directory_path; /* Owned; candidates borrow from their original. */
    char storage_revision[65];
#endif
    char message[192];
    /* Input the last refused edit was about: 1 name, 2 URL, 0 none. */
    int invalid_field;
} profile_config;

bool profiles_set_mode(profile_config *config, app_mode mode);
/* Saves this device's settings in one write; shortcuts and accessibility may
 * be NULL to keep them. */
bool profiles_set_settings(profile_config *config, app_mode mode, language_choice language,
                           const audio_config *audio, const keymap_overrides *shortcuts,
                           const accessibility_config *accessibility);
/* Saves the command keys that differ from the defaults; ids this build does
 * not know are the caller's to keep (keymap_table_to_overrides). */
bool profiles_set_shortcuts(profile_config *config, const keymap_overrides *shortcuts);
bool profiles_set_member_grouping(profile_config *config, bool by_status);
bool profiles_set_player_volume(profile_config *config, int level);
profile_config *profiles_create(void);
void profiles_destroy(profile_config *config);
bool profiles_reload(profile_config *config);
/* Copies where this device's configuration file is kept, for display; the path
 * is local text, not validated UTF-8. False, with output empty, when unknown. */
bool profiles_path(const profile_config *config, char *output, size_t size);
const server_profile *profiles_find(const profile_config *config, const char *id);
bool profiles_update(profile_config *config, const char *id, const char *name, const char *url,
                     bool allow_local_http, char output_id[PROFILE_ID_BYTES]);
bool profiles_remove(profile_config *config, const char *id);
bool profiles_select(profile_config *config, const char *id, bool allow_local_http);
bool profiles_set_rate(profile_config *config, double requests_per_second, unsigned burst);
bool profiles_set_notifications(profile_config *config, bool desktop, bool preview);
bool profiles_set_sounds(profile_config *config, bool sounds);
bool profiles_set_update_check(profile_config *config, bool automatic);

#endif

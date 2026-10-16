#ifndef PAPO_PROFILES_STORAGE_H
#define PAPO_PROFILES_STORAGE_H
#include "config/profiles.h"
/* Shared by the platform adapters; candidate configs borrow the storage. */
bool profiles_random_id(char output[PROFILE_ID_BYTES]);
bool profiles_storage_open(profile_config *config);
void profiles_storage_close(profile_config *config);
/* 1 = loaded, 0 = absent, -1 = preserved but inaccessible. malloc ownership. */
int profiles_storage_read(profile_config *config, char **data, size_t *length);
bool profiles_storage_commit(profile_config *config, const char *serialized);
bool profiles_storage_save(profile_config *config, bool (*serialize)(profile_config *));
#endif

#include "config/profiles_storage.h"
#include "platform/windows/files.h"
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *filename(const profile_config *config, const char *name)
{
    return g_build_filename(config->directory_path, name, NULL);
}
static bool error(profile_config *config, const char *message)
{
    snprintf(config->message, sizeof(config->message), "%s", message);
    return false;
}
bool profiles_storage_open(profile_config *config)
{
    config->directory_path = papo_win_directory(false);
    config->directory = config->directory_path ? 0 : -1;
    return config->directory_path ||
           error(config, "Não foi possível abrir a configuração privada.");
}
void profiles_storage_close(profile_config *config)
{
    free(config->directory_path);
    config->directory_path = NULL;
    config->directory = -1;
}
int profiles_storage_read(profile_config *config, char **data, size_t *length)
{
    char *path = filename(config, "config.json");
    int result = papo_win_read_private(path, 256U * 1024U, data, length);
    g_free(path);
    if (result == 1) {
        char *hash = g_compute_checksum_for_data(G_CHECKSUM_SHA256, (const guchar *)*data, *length);
        snprintf(config->storage_revision, sizeof(config->storage_revision), "%s", hash);
        g_free(hash);
    }
    return result;
}
static bool unchanged(profile_config *config)
{
    char *data = NULL, *path = filename(config, "config.json");
    size_t length = 0;
    int result = papo_win_read_private(path, 256U * 1024U, &data, &length);
    g_free(path);
    char *hash = result == 1
                     ? g_compute_checksum_for_data(G_CHECKSUM_SHA256, (const guchar *)data, length)
                     : NULL;
    bool ok = result == 0
                  ? !config->existed
                  : result == 1 && config->existed && !strcmp(hash, config->storage_revision);
    g_free(hash);
    free(data);
    return ok;
}
bool profiles_storage_commit(profile_config *config, const char *serialized)
{
    char *path = filename(config, "config.json");
    bool ok = unchanged(config) && papo_win_write_private(path, serialized, strlen(serialized));
    g_free(path);
    if (!ok)
        return error(config, "Não foi possível salvar. Os perfis anteriores foram preservados.");
    char *hash = g_compute_checksum_for_string(G_CHECKSUM_SHA256, serialized, -1);
    snprintf(config->storage_revision, sizeof(config->storage_revision), "%s", hash);
    g_free(hash);
    config->existed = true;
    snprintf(config->message, sizeof(config->message), "Perfis salvos; senhas não são salvas.");
    return true;
}
bool profiles_storage_save(profile_config *config, bool (*serialize)(profile_config *))
{
    char *path = filename(config, "config.lock");
    HANDLE lock = papo_win_lock(path);
    g_free(path);
    if (lock == INVALID_HANDLE_VALUE)
        return error(config, "Configuração ocupada por outro processo. Tente salvar novamente.");
    bool ok =
        unchanged(config)
            ? serialize(config)
            : error(config, "A configuração mudou em outro processo. Use Ctrl+R antes de salvar.");
    CloseHandle(lock);
    return ok;
}

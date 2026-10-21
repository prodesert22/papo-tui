#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "config/profiles.h"
#include "config/profiles_storage.h"
#include "net/auth/client.h"
#include "net/rate_limit.h"
#include "platform/runtime.h"

#include <curl/curl.h>
#include <errno.h>
#include <fcntl.h>
#include <jansson.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CONFIG_BYTES (256U * 1024U)

static bool error(profile_config *config, const char *message)
{
    snprintf(config->message, sizeof(config->message), "%s", message);
    return false;
}

static bool identifier(const char *value)
{
    if (!value || strlen(value) != PROFILE_ID_BYTES - 1)
        return false;
    for (size_t i = 0; value[i]; ++i)
        if (!((value[i] >= '0' && value[i] <= '9') || (value[i] >= 'a' && value[i] <= 'f')))
            return false;
    return true;
}

bool profiles_random_id(char output[PROFILE_ID_BYTES])
{
    unsigned char bytes[16];
    if (!papo_random_bytes(bytes, sizeof(bytes)))
        return false;
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < sizeof(bytes); ++i) {
        output[2 * i] = hex[bytes[i] >> 4];
        output[2 * i + 1] = hex[bytes[i] & 15];
    }
    output[32] = 0;
    return true;
}

static bool profile_name(const char *input, char output[PROFILE_NAME_BYTES])
{
    if (!input)
        return false;
    while (*input == ' ')
        ++input;
    size_t length = strlen(input);
    while (length && input[length - 1] == ' ')
        --length;
    if (!length || length >= PROFILE_NAME_BYTES)
        return false;
    for (size_t i = 0; i < length; ++i) {
        unsigned char value = (unsigned char)input[i];
        if (value < 32 || value == 127 ||
            (value == 0xc2 && i + 1 < length && (unsigned char)input[i + 1] >= 0x80 &&
             (unsigned char)input[i + 1] <= 0x9f))
            return false;
    }
    memcpy(output, input, length);
    output[length] = '\0';
    json_t *check = json_string(output); /* Jansson validates UTF-8. */
    if (!check)
        return false;
    json_decref(check);
    return true;
}

static bool profile_url(profile_config *config, const char *input, bool allow_http,
                        char output[PROFILE_URL_BYTES])
{
    char reason[192];
    char *normalized = auth_normalize_url(input, reason, sizeof(reason));
    if (!normalized)
        return error(config, reason);
    bool result = true;
    if (strlen(normalized) >= PROFILE_URL_BYTES)
        result = error(config, "URL muito longa; use até 2048 bytes UTF-8.");
    else if (!allow_http && strncmp(normalized, "http://", 7) == 0)
        result = error(config, "HTTP local exige iniciar com --allow-local-http. Prefira HTTPS.");
    else
        snprintf(output, PROFILE_URL_BYTES, "%s", normalized);
    curl_free(normalized);
    return result;
}

const server_profile *profiles_find(const profile_config *config, const char *id)
{
    if (!config || !id)
        return NULL;
    for (size_t i = 0; i < config->count; ++i)
        if (strcmp(config->profiles[i].id, id) == 0)
            return &config->profiles[i];
    return NULL;
}

static const char *json_text(json_t *value, size_t limit)
{
    if (!json_is_string(value))
        return NULL;
    const char *text = json_string_value(value);
    size_t length = json_string_length(value);
    return length < limit && strlen(text) == length ? text : NULL;
}

static bool decode(profile_config *config, json_t *root)
{
    config->mode = APP_MODE_NORMAL;
    config->player_volume = PLAYER_VOLUME_DEFAULT;
    if (!json_is_object(root) || !json_is_integer(json_object_get(root, "version")))
        return false;
    json_int_t version = json_integer_value(json_object_get(root, "version"));
    if (version == 1) {
        if (json_object_size(root) != 3)
            return false;
    } else if (version == 2 || version == 3 || version == 4 || version == 5 || version == 6) {
        json_t *rate = json_object_get(root, "requests_per_second");
        json_t *burst = json_object_get(root, "request_burst");
        if (json_object_size(root) != (version == 2   ? 5U
                                       : version == 3 ? 7U
                                       : version == 4 ? 8U
                                       : version == 5 ? 9U
                                                      : 10U) ||
            !json_is_number(rate) || !json_is_integer(burst) || json_integer_value(burst) <= 0 ||
            json_integer_value(burst) > UINT_MAX ||
            !net_rate_parameters_valid(json_number_value(rate),
                                       (unsigned)json_integer_value(burst)))
            return false;
        config->requests_per_second = json_number_value(rate);
        config->request_burst = (unsigned)json_integer_value(burst);
        if (version >= 3) {
            json_t *desktop = json_object_get(root, "desktop_notifications"),
                   *preview = json_object_get(root, "notification_preview");
            if (!json_is_boolean(desktop) || !json_is_boolean(preview))
                return false;
            config->desktop_notifications = json_is_true(desktop);
            config->notification_preview = json_is_true(preview);
        }
    } else
        return false;
    if (version >= 4 &&
        !app_mode_parse(json_text(json_object_get(root, "app_mode"), 16), &config->mode))
        return false;
    if (version >= 5) {
        json_t *group = json_object_get(root, "members_by_status");
        if (!json_is_boolean(group))
            return false;
        config->members_by_status = json_is_true(group);
    }
    if (version >= 6) {
        json_t *volume = json_object_get(root, "player_volume");
        if (!json_is_integer(volume) || json_integer_value(volume) < PLAYER_VOLUME_MIN ||
            json_integer_value(volume) > PLAYER_VOLUME_MAX)
            return false;
        config->player_volume = (int)json_integer_value(volume);
    }
    json_t *array = json_object_get(root, "profiles");
    json_t *selected = json_object_get(root, "selected_profile");
    if (!json_is_array(array) || json_array_size(array) > PROFILE_LIMIT)
        return false;
    for (size_t i = 0; i < json_array_size(array); ++i) {
        json_t *item = json_array_get(array, i);
        const char *id = json_text(json_object_get(item, "id"), PROFILE_ID_BYTES);
        const char *name = json_text(json_object_get(item, "name"), PROFILE_NAME_BYTES);
        const char *url = json_text(json_object_get(item, "url"), PROFILE_URL_BYTES);
        if (!json_is_object(item) || json_object_size(item) != 3 || !identifier(id) ||
            profiles_find(config, id))
            return false;
        server_profile *profile = &config->profiles[config->count];
        if (!profile_name(name, profile->name) || !profile_url(config, url, true, profile->url))
            return false;
        snprintf(profile->id, sizeof(profile->id), "%s", id);
        ++config->count;
    }
    if (!json_is_null(selected)) {
        const char *id = json_text(selected, PROFILE_ID_BYTES);
        if (!profiles_find(config, id))
            return false;
        snprintf(config->selected, sizeof(config->selected), "%s", id);
    }
    return true;
}

bool profiles_reload(profile_config *config)
{
    profiles_storage_close(config);
    memset(config, 0, sizeof(*config));
    config->directory = -1;
    config->requests_per_second = PAPO_REQUESTS_PER_SECOND;
    config->request_burst = PAPO_REQUEST_BURST;
    config->desktop_notifications = config->notification_preview = true;
    config->player_volume = PLAYER_VOLUME_DEFAULT;
    player_volume_set(PLAYER_VOLUME_DEFAULT);
    if (!profiles_storage_open(config))
        return false;
    char *buffer = NULL;
    size_t length = 0;
    int loaded = profiles_storage_read(config, &buffer, &length);
    if (loaded < 0)
        return error(config,
                     "Configuração inacessível; o arquivo foi preservado. Corrija-o e use Ctrl+R.");
    if (!loaded) {
        config->writable = true;
        net_rate_configure(config->requests_per_second, config->request_burst);
        snprintf(config->message, sizeof(config->message), "Adicione um servidor para começar.");
        return true;
    }
    bool valid = true;
    json_error_t diagnostic;
    json_t *root = valid ? json_loadb(buffer, length, JSON_REJECT_DUPLICATES, &diagnostic) : NULL;
    free(buffer);
    valid = root && decode(config, root);
    json_decref(root);
    if (!valid) {
        memset(config->profiles, 0, sizeof(config->profiles));
        config->count = 0;
        config->selected[0] = '\0';
        config->requests_per_second = PAPO_REQUESTS_PER_SECOND;
        config->request_burst = PAPO_REQUEST_BURST;
        config->desktop_notifications = config->notification_preview = true;
        config->player_volume = PLAYER_VOLUME_DEFAULT;
        player_volume_set(PLAYER_VOLUME_DEFAULT);
        return error(
            config,
            "Configuração inválida ou incompatível; arquivo preservado. Corrija-o e use Ctrl+R.");
    }
    config->writable = config->existed = true;
    player_volume_set(config->player_volume);
    if (!net_rate_configure(config->requests_per_second, config->request_burst))
        return error(
            config, "Perfis carregados; falha no relógio impediu aplicar os limites nesta sessão.");
    snprintf(config->message, sizeof(config->message),
             "Perfis carregados; senhas e cookies não são salvos.");
    return true;
}

profile_config *profiles_create(void)
{
    profile_config *config = calloc(1, sizeof(*config));
    if (!config)
        return NULL;
    config->directory = -1;
    profiles_reload(config);
    return config;
}

void profiles_destroy(profile_config *config)
{
    if (!config)
        return;
    profiles_storage_close(config);
    free(config);
}

static bool save_locked(profile_config *config)
{
    json_t *array = json_array();
    if (!array)
        return error(config, "Memória insuficiente para salvar os perfis.");
    for (size_t i = 0; i < config->count; ++i) {
        const server_profile *profile = &config->profiles[i];
        json_t *item = json_pack("{s:s,s:s,s:s}", "id", profile->id, "name", profile->name, "url",
                                 profile->url);
        if (!item || json_array_append_new(array, item) < 0) {
            json_decref(array);
            return error(config, "Memória insuficiente para salvar os perfis.");
        }
    }
    json_t *selection = *config->selected ? json_string(config->selected) : json_null();
    json_t *root =
        selection ? json_pack("{s:i,s:O,s:O,s:f,s:I,s:b,s:b,s:s,s:b,s:i}", "version", 6, "profiles",
                              array, "selected_profile", selection, "requests_per_second",
                              config->requests_per_second, "request_burst",
                              (json_int_t)config->request_burst, "desktop_notifications",
                              config->desktop_notifications, "notification_preview",
                              config->notification_preview, "app_mode", app_mode_name(config->mode),
                              "members_by_status", config->members_by_status, "player_volume",
                              (json_int_t)config->player_volume)
                  : NULL;
    json_decref(selection);
    json_decref(array);
    char *serialized = root ? json_dumps(root, JSON_INDENT(2) | JSON_SORT_KEYS) : NULL;
    json_decref(root);
    if (!serialized)
        return error(config, "Memória insuficiente para salvar os perfis.");
    if (strlen(serialized) > CONFIG_BYTES) {
        free(serialized);
        return error(config, "A configuração ultrapassou o limite de 256 KiB.");
    }
    bool result = profiles_storage_commit(config, serialized);
    free(serialized);
    return result;
}

static bool save(profile_config *config)
{
    return profiles_storage_save(config, save_locked);
}

static profile_config *candidate(profile_config *config)
{
    if (!config->writable) {
        error(
            config,
            "Configuração bloqueada; corrija o arquivo e use Ctrl+R. Nenhum perfil foi alterado.");
        return NULL;
    }
    profile_config *copy = malloc(sizeof(*copy));
    if (!copy) {
        error(config, "Memória insuficiente para alterar os perfis.");
        return NULL;
    }
    *copy = *config;
    return copy;
}

static bool commit(profile_config *config, profile_config *copy)
{
    bool result = save(copy);
    if (result)
        *config = *copy;
    else
        snprintf(config->message, sizeof(config->message), "%s", copy->message);
    free(copy); /* Descriptor is shared with config, never owned by this copy. */
    return result;
}

bool profiles_update(profile_config *config, const char *id, const char *name, const char *url,
                     bool allow_local_http, char output_id[PROFILE_ID_BYTES])
{
    profile_config *copy = candidate(config);
    if (!copy)
        return false;
    size_t index = copy->count;
    if (id && *id) {
        const server_profile *found = profiles_find(copy, id);
        if (!found) {
            free(copy);
            return error(config, "Perfil não encontrado. Use Ctrl+R.");
        }
        index = (size_t)(found - copy->profiles);
    } else if (index >= PROFILE_LIMIT) {
        free(copy);
        return error(config, "Limite de 64 perfis atingido.");
    }
    server_profile *profile = &copy->profiles[index];
    if (!profile_name(name, profile->name)) {
        free(copy);
        return error(config, "Informe um nome sem controles, com até 128 bytes UTF-8.");
    }
    if (!profile_url(config, url, allow_local_http, profile->url)) {
        free(copy);
        return false;
    }
    if (index == copy->count) {
        do {
            if (!profiles_random_id(profile->id)) {
                free(copy);
                return error(config, "Não foi possível gerar o ID do perfil.");
            }
        } while (profiles_find(copy, profile->id));
        ++copy->count;
    }
    char saved_id[PROFILE_ID_BYTES];
    snprintf(saved_id, sizeof(saved_id), "%s", profile->id);
    bool result = commit(config, copy);
    if (result && output_id)
        snprintf(output_id, PROFILE_ID_BYTES, "%s", saved_id);
    return result;
}

bool profiles_remove(profile_config *config, const char *id)
{
    const server_profile *found = profiles_find(config, id);
    if (!found)
        return error(config, "Perfil não encontrado.");
    size_t index = (size_t)(found - config->profiles);
    profile_config *copy = candidate(config);
    if (!copy)
        return false;
    if (strcmp(copy->selected, id) == 0)
        copy->selected[0] = '\0';
    memmove(copy->profiles + index, copy->profiles + index + 1,
            (copy->count - index - 1) * sizeof(copy->profiles[0]));
    memset(copy->profiles + --copy->count, 0, sizeof(copy->profiles[0]));
    return commit(config, copy);
}

bool profiles_select(profile_config *config, const char *id, bool allow_local_http)
{
    const server_profile *found = profiles_find(config, id);
    if (!found)
        return error(config, "Perfil não encontrado.");
    char url[PROFILE_URL_BYTES];
    if (!profile_url(config, found->url, allow_local_http, url))
        return false;
    profile_config *copy = candidate(config);
    if (!copy)
        return false;
    snprintf(copy->selected, sizeof(copy->selected), "%s", id);
    return commit(config, copy);
}

bool profiles_set_rate(profile_config *config, double requests_per_second, unsigned burst)
{
    if (!net_rate_parameters_valid(requests_per_second, burst))
        return error(config, "Use uma taxa positiva e finita e um burst inteiro positivo.");
    profile_config *copy = candidate(config);
    if (!copy)
        return false;
    copy->requests_per_second = requests_per_second;
    copy->request_burst = burst;
    if (!commit(config, copy))
        return false;
    if (!net_rate_configure(requests_per_second, burst))
        snprintf(config->message, sizeof(config->message),
                 "Limites salvos; falha no relógio impediu aplicá-los nesta sessão.");
    else if (strncmp(config->message, "Perfis salvos;", strlen("Perfis salvos;")) == 0)
        snprintf(config->message, sizeof(config->message), "Limites salvos: %.8g req/s · burst %u.",
                 requests_per_second, burst);
    return true;
}
bool profiles_set_notifications(profile_config *config, bool desktop, bool preview)
{
    profile_config *copy = candidate(config);
    if (!copy)
        return false;
    copy->desktop_notifications = desktop;
    copy->notification_preview = preview;
    return commit(config, copy);
}

bool profiles_set_mode(profile_config *config, app_mode mode)
{
    if (mode < APP_MODE_NORMAL || mode > APP_MODE_ULTRALIGHT)
        return error(config, "Modo inválido.");
    profile_config *copy = candidate(config);
    if (!copy)
        return false;
    copy->mode = mode;
    return commit(config, copy);
}

bool profiles_set_member_grouping(profile_config *config, bool by_status)
{
    profile_config *copy = candidate(config);
    if (!copy)
        return false;
    copy->members_by_status = by_status;
    return commit(config, copy);
}
bool profiles_set_player_volume(profile_config *config, int level)
{
    if (level < PLAYER_VOLUME_MIN || level > PLAYER_VOLUME_MAX)
        return error(config, "Escolha um volume entre 0 e 100.");
    profile_config *copy = candidate(config);
    if (!copy)
        return false;
    copy->player_volume = level;
    if (!commit(config, copy))
        return false;
    player_volume_set(level);
    return true;
}

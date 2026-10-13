#define _POSIX_C_SOURCE 200809L
#include "config/profiles.h"
#include "net/auth.h"
#include "net/rate_limit.h"

#include <curl/curl.h>
#include <jansson.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>
#include <sys/file.h>
#include <unistd.h>

#define CONFIG_BYTES (256U * 1024U)

static bool error(profile_config *config, const char *message)
{
    snprintf(config->message, sizeof(config->message), "%s", message);
    return false;
}

static bool identifier(const char *value)
{
    if (!value || strlen(value) != PROFILE_ID_BYTES - 1) return false;
    for (size_t i = 0; value[i]; ++i)
        if (!((value[i] >= '0' && value[i] <= '9') || (value[i] >= 'a' && value[i] <= 'f'))) return false;
    return true;
}

static bool random_id(char output[PROFILE_ID_BYTES])
{
    unsigned char bytes[16];
    size_t used = 0;
    while (used < sizeof(bytes)) {
        ssize_t count = getrandom(bytes + used, sizeof(bytes) - used, 0);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) return false;
        used += (size_t)count;
    }
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < sizeof(bytes); ++i) {
        output[2 * i] = hex[bytes[i] >> 4];
        output[2 * i + 1] = hex[bytes[i] & 15];
    }
    output[32] = '\0';
    return true;
}

static bool profile_name(const char *input, char output[PROFILE_NAME_BYTES])
{
    if (!input) return false;
    while (*input == ' ') ++input;
    size_t length = strlen(input);
    while (length && input[length - 1] == ' ') --length;
    if (!length || length >= PROFILE_NAME_BYTES) return false;
    for (size_t i = 0; i < length; ++i) {
        unsigned char value = (unsigned char)input[i];
        if (value < 32 || value == 127 ||
            (value == 0xc2 && i + 1 < length && (unsigned char)input[i + 1] >= 0x80 &&
             (unsigned char)input[i + 1] <= 0x9f)) return false;
    }
    memcpy(output, input, length);
    output[length] = '\0';
    json_t *check = json_string(output); /* Jansson validates UTF-8. */
    if (!check) return false;
    json_decref(check);
    return true;
}

static bool profile_url(profile_config *config, const char *input, bool allow_http,
                        char output[PROFILE_URL_BYTES])
{
    char reason[192];
    char *normalized = auth_normalize_url(input, reason, sizeof(reason));
    if (!normalized) return error(config, reason);
    bool result = true;
    if (strlen(normalized) >= PROFILE_URL_BYTES)
        result = error(config, "URL muito longa; use até 2048 bytes UTF-8.");
    else if (!allow_http && strncmp(normalized, "http://", 7) == 0)
        result = error(config, "HTTP local exige iniciar com --allow-local-http. Prefira HTTPS.");
    else snprintf(output, PROFILE_URL_BYTES, "%s", normalized);
    curl_free(normalized);
    return result;
}

const server_profile *profiles_find(const profile_config *config, const char *id)
{
    if (!config || !id) return NULL;
    for (size_t i = 0; i < config->count; ++i)
        if (strcmp(config->profiles[i].id, id) == 0) return &config->profiles[i];
    return NULL;
}

static bool mkdir_parents(char *path)
{
    for (char *p = path + 1; ; ++p) {
        if (*p != '/' && *p) continue;
        char saved = *p;
        *p = '\0';
        int result = mkdir(path, 0700);
        *p = saved;
        if (result < 0 && errno != EEXIST) return false;
        if (!saved) break;
    }
    return true;
}

static int open_directory(profile_config *config)
{
    const char *base = getenv("XDG_CONFIG_HOME");
    bool xdg = base && *base == '/';
    if (!xdg) base = getenv("HOME");
    char path[PATH_MAX];
    if (!base || *base != '/' || snprintf(path, sizeof(path), "%s%s", base, xdg ? "" : "/.config") >= (int)sizeof(path)) {
        error(config, "Não foi possível localizar a configuração XDG. Defina HOME ou XDG_CONFIG_HOME absoluto.");
        return -1;
    }
    if (!mkdir_parents(path)) {
        error(config, "Não foi possível criar o diretório de configuração.");
        return -1;
    }
    int root = open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (root < 0) goto failed;
    if (mkdirat(root, "papo-tui", 0700) < 0 && errno != EEXIST) { close(root); goto failed; }
    int directory = openat(root, "papo-tui", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    close(root);
    if (directory < 0) goto failed;
    struct stat status;
    if (fstat(directory, &status) < 0 || status.st_uid != getuid() ||
        ((status.st_mode & 07777) != 0700 && fchmod(directory, 0700) < 0)) {
        close(directory); goto failed;
    }
    return directory;
failed:
    error(config, "Não foi possível abrir um diretório privado de configuração.");
    return -1;
}

static const char *json_text(json_t *value, size_t limit)
{
    if (!json_is_string(value)) return NULL;
    const char *text = json_string_value(value);
    size_t length = json_string_length(value);
    return length < limit && strlen(text) == length ? text : NULL;
}

static bool decode(profile_config *config, json_t *root)
{
    if (!json_is_object(root) || !json_is_integer(json_object_get(root, "version"))) return false;
    json_int_t version = json_integer_value(json_object_get(root, "version"));
    if (version == 1) {
        if (json_object_size(root) != 3) return false;
    } else if (version == 2) {
        json_t *rate = json_object_get(root, "requests_per_second");
        json_t *burst = json_object_get(root, "request_burst");
        if (json_object_size(root) != 5 || !json_is_number(rate) || !json_is_integer(burst) ||
            json_integer_value(burst) <= 0 || json_integer_value(burst) > UINT_MAX ||
            !net_rate_parameters_valid(json_number_value(rate), (unsigned)json_integer_value(burst))) return false;
        config->requests_per_second = json_number_value(rate);
        config->request_burst = (unsigned)json_integer_value(burst);
    } else return false;
    json_t *array = json_object_get(root, "profiles");
    json_t *selected = json_object_get(root, "selected_profile");
    if (!json_is_array(array) || json_array_size(array) > PROFILE_LIMIT) return false;
    for (size_t i = 0; i < json_array_size(array); ++i) {
        json_t *item = json_array_get(array, i);
        const char *id = json_text(json_object_get(item, "id"), PROFILE_ID_BYTES);
        const char *name = json_text(json_object_get(item, "name"), PROFILE_NAME_BYTES);
        const char *url = json_text(json_object_get(item, "url"), PROFILE_URL_BYTES);
        if (!json_is_object(item) || json_object_size(item) != 3 || !identifier(id) || profiles_find(config, id)) return false;
        server_profile *profile = &config->profiles[config->count];
        if (!profile_name(name, profile->name) || !profile_url(config, url, true, profile->url)) return false;
        snprintf(profile->id, sizeof(profile->id), "%s", id);
        ++config->count;
    }
    if (!json_is_null(selected)) {
        const char *id = json_text(selected, PROFILE_ID_BYTES);
        if (!profiles_find(config, id)) return false;
        snprintf(config->selected, sizeof(config->selected), "%s", id);
    }
    return true;
}

bool profiles_reload(profile_config *config)
{
    if (config->directory >= 0) close(config->directory);
    memset(config, 0, sizeof(*config));
    config->requests_per_second = PAPO_REQUESTS_PER_SECOND;
    config->request_burst = PAPO_REQUEST_BURST;
    config->directory = open_directory(config);
    if (config->directory < 0) return false;
    int fd = openat(config->directory, "config.json", O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0) {
        if (errno != ENOENT) return error(config, "Configuração inacessível; o arquivo foi preservado. Corrija-o e use Ctrl+R.");
        config->writable = true;
        net_rate_configure(config->requests_per_second, config->request_burst);
        snprintf(config->message, sizeof(config->message), "Adicione um servidor para começar.");
        return true;
    }
    bool valid = fstat(fd, &config->revision) == 0 && S_ISREG(config->revision.st_mode) &&
                 config->revision.st_uid == getuid() && config->revision.st_nlink == 1 &&
                 config->revision.st_size >= 0 && config->revision.st_size <= (off_t)CONFIG_BYTES &&
                 ((config->revision.st_mode & 07777) == 0600 || fchmod(fd, 0600) == 0) &&
                 fstat(fd, &config->revision) == 0;
    char *buffer = valid ? malloc(CONFIG_BYTES + 1) : NULL;
    size_t length = 0;
    if (!buffer) valid = false;
    while (valid) {
        ssize_t count = read(fd, buffer + length, CONFIG_BYTES + 1 - length);
        if (count < 0 && errno == EINTR) continue;
        if (count < 0 || (count > 0 && (size_t)count > CONFIG_BYTES - length)) { valid = false; break; }
        if (!count) break;
        length += (size_t)count;
    }
    close(fd);
    json_error_t diagnostic;
    json_t *root = valid ? json_loadb(buffer, length, JSON_REJECT_DUPLICATES, &diagnostic) : NULL;
    free(buffer);
    valid = root && decode(config, root);
    json_decref(root);
    if (!valid) {
        config->count = 0;
        config->selected[0] = '\0';
        return error(config, "Configuração inválida ou incompatível; arquivo preservado. Corrija-o e use Ctrl+R.");
    }
    config->writable = config->existed = true;
    if (!net_rate_configure(config->requests_per_second, config->request_burst))
        return error(config, "Perfis carregados; falha no relógio impediu aplicar os limites nesta sessão.");
    snprintf(config->message, sizeof(config->message), "Perfis carregados; senhas e cookies não são salvos.");
    return true;
}

profile_config *profiles_create(void)
{
    profile_config *config = calloc(1, sizeof(*config));
    if (!config) return NULL;
    config->directory = -1;
    profiles_reload(config);
    return config;
}

void profiles_destroy(profile_config *config)
{
    if (!config) return;
    if (config->directory >= 0) close(config->directory);
    free(config);
}

static bool unchanged(const profile_config *config)
{
    struct stat current;
    if (fstatat(config->directory, "config.json", &current, AT_SYMLINK_NOFOLLOW) < 0)
        return errno == ENOENT && !config->existed;
    const struct stat *old = &config->revision;
    return config->existed && S_ISREG(current.st_mode) && current.st_dev == old->st_dev &&
           current.st_ino == old->st_ino && current.st_size == old->st_size &&
           current.st_mtim.tv_sec == old->st_mtim.tv_sec && current.st_mtim.tv_nsec == old->st_mtim.tv_nsec &&
           current.st_ctim.tv_sec == old->st_ctim.tv_sec && current.st_ctim.tv_nsec == old->st_ctim.tv_nsec;
}

static bool save_locked(profile_config *config)
{
    if (!unchanged(config)) return error(config, "A configuração mudou em outro processo. Use Ctrl+R antes de salvar.");
    json_t *array = json_array();
    if (!array) return error(config, "Memória insuficiente para salvar os perfis.");
    for (size_t i = 0; i < config->count; ++i) {
        const server_profile *profile = &config->profiles[i];
        json_t *item = json_pack("{s:s,s:s,s:s}", "id", profile->id, "name", profile->name, "url", profile->url);
        if (!item || json_array_append_new(array, item) < 0) {
            json_decref(array);
            return error(config, "Memória insuficiente para salvar os perfis.");
        }
    }
    json_t *selection = *config->selected ? json_string(config->selected) : json_null();
    json_t *root = selection ? json_pack("{s:i,s:O,s:O,s:f,s:I}", "version", 2, "profiles", array,
                                         "selected_profile", selection, "requests_per_second", config->requests_per_second,
                                         "request_burst", (json_int_t)config->request_burst) : NULL;
    json_decref(selection);
    json_decref(array);
    char *serialized = root ? json_dumps(root, JSON_INDENT(2) | JSON_SORT_KEYS) : NULL;
    json_decref(root);
    if (!serialized) return error(config, "Memória insuficiente para salvar os perfis.");
    if (strlen(serialized) > CONFIG_BYTES) { free(serialized); return error(config, "A configuração ultrapassou o limite de 256 KiB."); }
    char token[PROFILE_ID_BYTES], temporary[64];
    int fd = -1;
    bool result = false;
    if (!random_id(token)) { free(serialized); return error(config, "Não foi possível criar o arquivo temporário."); }
    snprintf(temporary, sizeof(temporary), "config.tmp.%s", token);
    fd = openat(config->directory, temporary, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd < 0) goto done;
    size_t length = strlen(serialized), written = 0;
    while (written < length) {
        ssize_t count = write(fd, serialized + written, length - written);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) goto done;
        written += (size_t)count;
    }
    struct stat committed;
    if (fchmod(fd, 0600) < 0 || fsync(fd) < 0 || fstat(fd, &committed) < 0 || !unchanged(config)) goto done;
    /* Close errors are checked before replacing the original file. */
    int closed = close(fd);
    fd = -1;
    if (closed < 0 || renameat(config->directory, temporary, config->directory, "config.json") < 0) goto done;
    config->existed = true;
    config->revision = committed;
    /* rename changes ctime: record the committed file's revision. */
    if (fstatat(config->directory, "config.json", &config->revision, AT_SYMLINK_NOFOLLOW) < 0) config->writable = false;
    result = true;
    snprintf(config->message, sizeof(config->message), "%s", fsync(config->directory) == 0 ?
             "Perfis salvos; senhas e cookies permanecem somente em memória." :
             "Alteração salva; não foi possível confirmar a sincronização do diretório.");
done:
    if (fd >= 0) close(fd);
    if (!result) { unlinkat(config->directory, temporary, 0); error(config, "Não foi possível salvar. Os perfis anteriores foram preservados."); }
    free(serialized);
    return result;
}

static bool save(profile_config *config)
{
    if (flock(config->directory, LOCK_EX | LOCK_NB) < 0)
        return error(config, "Configuração ocupada por outro processo. Tente salvar novamente.");
    bool result = save_locked(config);
    flock(config->directory, LOCK_UN);
    return result;
}

static profile_config *candidate(profile_config *config)
{
    if (!config->writable) { error(config, "Configuração bloqueada; corrija o arquivo e use Ctrl+R. Nenhum perfil foi alterado."); return NULL; }
    profile_config *copy = malloc(sizeof(*copy));
    if (!copy) { error(config, "Memória insuficiente para alterar os perfis."); return NULL; }
    *copy = *config;
    return copy;
}

static bool commit(profile_config *config, profile_config *copy)
{
    bool result = save(copy);
    if (result) *config = *copy;
    else snprintf(config->message, sizeof(config->message), "%s", copy->message);
    free(copy); /* Descriptor is shared with config, never owned by this copy. */
    return result;
}

bool profiles_update(profile_config *config, const char *id, const char *name,
                     const char *url, bool allow_local_http, char output_id[PROFILE_ID_BYTES])
{
    profile_config *copy = candidate(config);
    if (!copy) return false;
    size_t index = copy->count;
    if (id && *id) {
        const server_profile *found = profiles_find(copy, id);
        if (!found) { free(copy); return error(config, "Perfil não encontrado. Use Ctrl+R."); }
        index = (size_t)(found - copy->profiles);
    } else if (index >= PROFILE_LIMIT) { free(copy); return error(config, "Limite de 64 perfis atingido."); }
    server_profile *profile = &copy->profiles[index];
    if (!profile_name(name, profile->name)) { free(copy); return error(config, "Informe um nome sem controles, com até 128 bytes UTF-8."); }
    if (!profile_url(config, url, allow_local_http, profile->url)) { free(copy); return false; }
    if (index == copy->count) {
        do {
            if (!random_id(profile->id)) { free(copy); return error(config, "Não foi possível gerar o ID do perfil."); }
        } while (profiles_find(copy, profile->id));
        ++copy->count;
    }
    char saved_id[PROFILE_ID_BYTES];
    snprintf(saved_id, sizeof(saved_id), "%s", profile->id);
    bool result = commit(config, copy);
    if (result && output_id) snprintf(output_id, PROFILE_ID_BYTES, "%s", saved_id);
    return result;
}

bool profiles_remove(profile_config *config, const char *id)
{
    const server_profile *found = profiles_find(config, id);
    if (!found) return error(config, "Perfil não encontrado.");
    size_t index = (size_t)(found - config->profiles);
    profile_config *copy = candidate(config);
    if (!copy) return false;
    if (strcmp(copy->selected, id) == 0) copy->selected[0] = '\0';
    memmove(copy->profiles + index, copy->profiles + index + 1,
            (copy->count - index - 1) * sizeof(copy->profiles[0]));
    memset(copy->profiles + --copy->count, 0, sizeof(copy->profiles[0]));
    return commit(config, copy);
}

bool profiles_select(profile_config *config, const char *id, bool allow_local_http)
{
    const server_profile *found = profiles_find(config, id);
    if (!found) return error(config, "Perfil não encontrado.");
    char url[PROFILE_URL_BYTES];
    if (!profile_url(config, found->url, allow_local_http, url)) return false;
    profile_config *copy = candidate(config);
    if (!copy) return false;
    snprintf(copy->selected, sizeof(copy->selected), "%s", id);
    return commit(config, copy);
}

bool profiles_set_rate(profile_config *config, double requests_per_second, unsigned burst)
{
    if (!net_rate_parameters_valid(requests_per_second, burst))
        return error(config, "Use uma taxa positiva e finita e um burst inteiro positivo.");
    profile_config *copy = candidate(config);
    if (!copy) return false;
    copy->requests_per_second = requests_per_second;
    copy->request_burst = burst;
    if (!commit(config, copy)) return false;
    if (!net_rate_configure(requests_per_second, burst))
        snprintf(config->message, sizeof(config->message), "Limites salvos; falha no relógio impediu aplicá-los nesta sessão.");
    else if (strncmp(config->message, "Perfis salvos;", strlen("Perfis salvos;")) == 0)
        snprintf(config->message, sizeof(config->message), "Limites salvos: %.8g req/s · burst %u.", requests_per_second, burst);
    return true;
}

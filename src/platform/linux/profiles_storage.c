#define _POSIX_C_SOURCE 200809L
#include "config/profiles_storage.h"
#include "platform/runtime.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <unistd.h>
#define CONFIG_BYTES (256U * 1024U)
static bool error(profile_config *config, const char *message)
{
    snprintf(config->message, sizeof(config->message), "%s", message);
    return false;
}
static bool mkdir_parents(char *path)
{
    for (char *p = path + 1;; ++p) {
        if (*p != '/' && *p)
            continue;
        char saved = *p;
        *p = '\0';
        int result = mkdir(path, 0700);
        *p = saved;
        if (result < 0 && errno != EEXIST)
            return false;
        if (!saved)
            break;
    }
    return true;
}
static int open_directory(profile_config *config)
{
    const char *base = getenv("XDG_CONFIG_HOME");
    bool xdg = base && *base == '/';
    if (!xdg)
        base = getenv("HOME");
    char path[PATH_MAX];
    if (!base || *base != '/' ||
        snprintf(path, sizeof(path), "%s%s", base, xdg ? "" : "/.config") >= (int)sizeof(path)) {
        error(config, "Não foi possível localizar a configuração XDG. Defina HOME ou "
                      "XDG_CONFIG_HOME absoluto.");
        return -1;
    }
    if (!mkdir_parents(path)) {
        error(config, "Não foi possível criar o diretório de configuração.");
        return -1;
    }
    int root = open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (root < 0)
        goto failed;
    if (mkdirat(root, "papo-tui", 0700) < 0 && errno != EEXIST) {
        close(root);
        goto failed;
    }
    int directory = openat(root, "papo-tui", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    close(root);
    if (directory < 0)
        goto failed;
    struct stat status;
    if (fstat(directory, &status) < 0 || status.st_uid != getuid() ||
        ((status.st_mode & 07777) != 0700 && fchmod(directory, 0700) < 0)) {
        close(directory);
        goto failed;
    }
    return directory;
failed:
    error(config, "Não foi possível abrir um diretório privado de configuração.");
    return -1;
}
static bool unchanged(const profile_config *config)
{
    struct stat current;
    if (fstatat(config->directory, "config.json", &current, AT_SYMLINK_NOFOLLOW) < 0)
        return errno == ENOENT && !config->existed;
    const struct stat *old = &config->revision;
    return config->existed && S_ISREG(current.st_mode) && current.st_dev == old->st_dev &&
           current.st_ino == old->st_ino && current.st_size == old->st_size &&
           current.st_mtim.tv_sec == old->st_mtim.tv_sec &&
           current.st_mtim.tv_nsec == old->st_mtim.tv_nsec &&
           current.st_ctim.tv_sec == old->st_ctim.tv_sec &&
           current.st_ctim.tv_nsec == old->st_ctim.tv_nsec;
}
bool profiles_storage_open(profile_config *config)
{
    config->directory = open_directory(config);
    return config->directory >= 0;
}
void profiles_storage_close(profile_config *config)
{
    if (config->directory >= 0)
        close(config->directory);
    config->directory = -1;
}
int profiles_storage_read(profile_config *config, char **data, size_t *size)
{
    int fd =
        openat(config->directory, "config.json", O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0)
        return errno == ENOENT ? 0 : -1;
    bool valid = fstat(fd, &config->revision) == 0 && S_ISREG(config->revision.st_mode) &&
                 config->revision.st_uid == getuid() && config->revision.st_nlink == 1 &&
                 config->revision.st_size >= 0 && config->revision.st_size <= (off_t)CONFIG_BYTES &&
                 ((config->revision.st_mode & 07777) == 0600 || fchmod(fd, 0600) == 0) &&
                 fstat(fd, &config->revision) == 0;
    char *buffer = valid ? malloc(CONFIG_BYTES + 1) : NULL;
    size_t length = 0;
    if (!buffer)
        valid = false;
    while (valid) {
        ssize_t count = read(fd, buffer + length, CONFIG_BYTES + 1 - length);
        if (count < 0 && errno == EINTR)
            continue;
        if (count < 0 || (count > 0 && (size_t)count > CONFIG_BYTES - length)) {
            valid = false;
            break;
        }
        if (!count)
            break;
        length += (size_t)count;
    }
    close(fd);
    if (!valid) {
        free(buffer);
        return -1;
    }
    *data = buffer;
    *size = length;
    return 1;
}
bool profiles_storage_commit(profile_config *config, const char *serialized)
{
    char token[PROFILE_ID_BYTES], temporary[64];
    int fd = -1;
    bool result = false;
    if (!profiles_random_id(token)) {

        return error(config, "Não foi possível criar o arquivo temporário.");
    }
    snprintf(temporary, sizeof(temporary), "config.tmp.%s", token);
    fd = openat(config->directory, temporary, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC,
                0600);
    if (fd < 0)
        goto done;
    size_t length = strlen(serialized), written = 0;
    while (written < length) {
        ssize_t count = write(fd, serialized + written, length - written);
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0)
            goto done;
        written += (size_t)count;
    }
    struct stat committed;
    if (fchmod(fd, 0600) < 0 || fsync(fd) < 0 || fstat(fd, &committed) < 0 || !unchanged(config))
        goto done;
    /* Close errors are checked before replacing the original file. */
    int closed = close(fd);
    fd = -1;
    if (closed < 0 || renameat(config->directory, temporary, config->directory, "config.json") < 0)
        goto done;
    config->existed = true;
    config->revision = committed;
    /* rename changes ctime: record the committed file's revision. */
    if (fstatat(config->directory, "config.json", &config->revision, AT_SYMLINK_NOFOLLOW) < 0)
        config->writable = false;
    result = true;
    snprintf(config->message, sizeof(config->message), "%s",
             fsync(config->directory) == 0
                 ? "Perfis salvos; senhas e cookies permanecem somente em memória."
                 : "Alteração salva; não foi possível confirmar a sincronização do diretório.");
done:
    if (fd >= 0)
        close(fd);
    if (!result) {
        unlinkat(config->directory, temporary, 0);
        error(config, "Não foi possível salvar. Os perfis anteriores foram preservados.");
    }

    return result;
}
bool profiles_storage_save(profile_config *config, bool (*serialize)(profile_config *))
{
    if (flock(config->directory, LOCK_EX | LOCK_NB) < 0)
        return error(config, "Configuração ocupada por outro processo. Tente salvar novamente.");
    bool result =
        unchanged(config)
            ? serialize(config)
            : error(config, "A configuração mudou em outro processo. Use Ctrl+R antes de salvar.");
    flock(config->directory, LOCK_UN);
    return result;
}

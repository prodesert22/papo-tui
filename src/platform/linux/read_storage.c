#define _POSIX_C_SOURCE 200809L
#include "config/read_storage.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
struct read_storage {
    int directory, lock;
};
static const char *file_name = "read-cursors.json";
static bool private_file(int fd)
{
    struct stat st;
    return fstat(fd, &st) == 0 && S_ISREG(st.st_mode) && st.st_uid == getuid() &&
           st.st_nlink == 1 && (st.st_mode & 07777) == 0600;
}
static int native_directory_open(void)
{
    const char *xdg = getenv("XDG_STATE_HOME"), *home = getenv("HOME");
    char path[4096];
    if (xdg && *xdg) {
        if (*xdg != '/' || snprintf(path, sizeof(path), "%s", xdg) >= (int)sizeof(path))
            return -1;
    } else if (!home || *home != '/' ||
               snprintf(path, sizeof(path), "%s/.local/state", home) >= (int)sizeof(path))
        return -1;
    for (char *p = path + 1; *p; ++p)
        if (*p == '/') {
            *p = 0;
            int rc = mkdir(path, 0700);
            *p = '/';
            if (rc && errno != EEXIST)
                return -1;
        }
    if (mkdir(path, 0700) && errno != EEXIST)
        return -1;
    int parent = open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (parent < 0)
        return -1;
    if (mkdirat(parent, "papo-tui", 0700) && errno != EEXIST) {
        close(parent);
        return -1;
    }
    int fd = openat(parent, "papo-tui", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    close(parent);
    struct stat st;
    if (fd >= 0 && (fstat(fd, &st) || st.st_uid != getuid() || (st.st_mode & 07777) != 0700)) {
        close(fd);
        return -1;
    }
    return fd;
}
json_t *read_storage_load(read_storage *storage)
{
    int directory = storage->directory;
    int fd = openat(directory, file_name, O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);
    if (fd < 0)
        return errno == ENOENT ? json_pack("{s:i,s:[]}", "version", 1, "contexts") : NULL;
    struct stat st;
    if (!private_file(fd) || fstat(fd, &st) || st.st_size < 1 || st.st_size > STATE_BYTES) {
        close(fd);
        return NULL;
    }
    size_t limit = (size_t)st.st_size, length = 0;
    char *buffer = malloc(limit + 1);
    if (!buffer) {
        close(fd);
        return NULL;
    }
    bool ok = true;
    while (length <= limit) {
        ssize_t n = read(fd, buffer + length, limit + 1 - length);
        if (n < 0 && errno == EINTR)
            continue;
        if (n < 0)
            ok = false;
        if (n <= 0)
            break;
        length += (size_t)n;
        if (length > limit) {
            ok = false;
            break;
        }
    }
    close(fd);
    json_t *root = ok ? json_loadb(buffer, length, JSON_REJECT_DUPLICATES, NULL) : NULL;
    free(buffer);
    if (root && !read_state_validate(root)) {
        json_decref(root);
        root = NULL;
    }
    return root;
}
bool read_storage_write(read_storage *storage, json_t *root)
{
    int directory = storage->directory;
    char *data = json_dumps(root, JSON_COMPACT);
    while (data && strlen(data) > STATE_BYTES &&
           json_array_size(json_object_get(root, "contexts")) > 1) {
        free(data);
        json_array_remove(json_object_get(root, "contexts"), 0);
        data = json_dumps(root, JSON_COMPACT);
    }
    if (!data || strlen(data) > STATE_BYTES) {
        free(data);
        return false;
    }
    char temporary[96];
    int fd = -1;
    for (int attempt = 0; attempt < 16 && fd < 0; ++attempt) {
        snprintf(temporary, sizeof(temporary), "read-cursors.%ld.%d.tmp", (long)getpid(), attempt);
        fd = openat(directory, temporary, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC,
                    0600);
        if (fd < 0 && errno != EEXIST)
            break;
    }
    bool ok = fd >= 0;
    size_t length = strlen(data), written = 0;
    while (ok && written < length) {
        ssize_t n = write(fd, data + written, length - written);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            ok = false;
        else
            written += (size_t)n;
    }
    free(data);
    if (ok && fsync(fd))
        ok = false;
    if (fd >= 0 && close(fd))
        ok = false;
    if (ok && renameat(directory, temporary, directory, file_name))
        ok = false;
    if (ok && fsync(directory))
        ok = false;
    if (!ok && fd >= 0)
        unlinkat(directory, temporary, 0);
    return ok;
}
read_storage *read_storage_open(void)
{
    int directory = native_directory_open();
    if (directory < 0)
        return NULL;
    read_storage *storage = malloc(sizeof(*storage));
    if (!storage) {
        close(directory);
        return NULL;
    }
    *storage = (read_storage){directory, -1};
    return storage;
}
void read_storage_close(read_storage *storage)
{
    if (!storage)
        return;
    if (storage->lock >= 0)
        close(storage->lock);
    close(storage->directory);
    free(storage);
}
bool read_storage_lock(read_storage *storage)
{
    storage->lock = openat(storage->directory, "read-cursors.lock",
                           O_RDWR | O_CREAT | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK, 0600);
    return storage->lock >= 0 && private_file(storage->lock) &&
           !flock(storage->lock, LOCK_EX | LOCK_NB);
}

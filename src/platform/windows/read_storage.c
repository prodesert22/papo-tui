#include "config/read_storage.h"
#include "platform/windows/files.h"
#include <glib.h>
#include <stdlib.h>
#include <string.h>
struct read_storage {
    char *path;
    HANDLE lock;
};
read_storage *read_storage_open(void)
{
    char *path = papo_win_directory(true);
    if (!path)
        return NULL;
    read_storage *storage = malloc(sizeof(*storage));
    if (!storage) {
        free(path);
        return NULL;
    }
    *storage = (read_storage){path, INVALID_HANDLE_VALUE};
    return storage;
}
void read_storage_close(read_storage *storage)
{
    if (!storage)
        return;
    if (storage->lock != INVALID_HANDLE_VALUE)
        CloseHandle(storage->lock);
    free(storage->path);
    free(storage);
}
bool read_storage_lock(read_storage *storage)
{
    char *path = g_build_filename(storage->path, "read-cursors.lock", NULL);
    storage->lock = papo_win_lock(path);
    g_free(path);
    return storage->lock != INVALID_HANDLE_VALUE;
}
json_t *read_storage_load(read_storage *storage)
{
    char *path = g_build_filename(storage->path, "read-cursors.json", NULL), *data = NULL;
    size_t length = 0;
    int loaded = papo_win_read_private(path, STATE_BYTES, &data, &length);
    g_free(path);
    json_t *root = loaded == 0   ? json_pack("{s:i,s:[]}", "version", 1, "contexts")
                   : loaded == 1 ? json_loadb(data, length, JSON_REJECT_DUPLICATES, NULL)
                                 : NULL;
    free(data);
    if (root && !read_state_validate(root)) {
        json_decref(root);
        return NULL;
    }
    return root;
}
bool read_storage_write(read_storage *storage, json_t *root)
{
    char *data = json_dumps(root, JSON_COMPACT);
    while (data && strlen(data) > STATE_BYTES &&
           json_array_size(json_object_get(root, "contexts")) > 1) {
        free(data);
        json_array_remove(json_object_get(root, "contexts"), 0);
        data = json_dumps(root, JSON_COMPACT);
    }
    char *path = g_build_filename(storage->path, "read-cursors.json", NULL);
    bool ok =
        data && strlen(data) <= STATE_BYTES && papo_win_write_private(path, data, strlen(data));
    g_free(path);
    free(data);
    return ok;
}

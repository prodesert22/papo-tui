#include "config/profiles.h"
#include "platform/runtime.h"
#include "platform/windows/files.h"
#include <assert.h>
#include <curl/curl.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <stdlib.h>
#include <string.h>

int main(void)
{
    assert(curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK);
    char *root = g_build_filename(g_get_tmp_dir(), "papo-config-á-XXXXXX", NULL);
    assert(papo_mkdtemp(root));
    g_setenv("XDG_CONFIG_HOME", root, TRUE);
    profile_config *first = profiles_create(), *stale = profiles_create();
    assert(first && stale && first->writable && stale->writable);
    char id[PROFILE_ID_BYTES], other[PROFILE_ID_BYTES];
    assert(profiles_update(first, NULL, "Instância á 😀", "https://one.example.test", false, id));
    assert(first->count == 1 && profiles_find(first, id));
    assert(!profiles_update(stale, NULL, "Stale", "https://two.example.test", false, other));
    assert(stale->count == 0 && profiles_reload(stale) && stale->count == 1);
    assert(!strcmp(stale->profiles[0].name, "Instância á 😀"));
    assert(profiles_set_notifications(stale, true, false));
    assert(profiles_reload(first) && first->desktop_notifications && !first->notification_preview);
    char *lockpath = g_build_filename(first->directory_path, "config.lock", NULL);
    HANDLE lock = papo_win_lock(lockpath);
    assert(lock != INVALID_HANDLE_VALUE);
    assert(!profiles_remove(first, id) && first->count == 1);
    CloseHandle(lock);
    assert(profiles_remove(first, id) && first->count == 0);
    char *file = g_build_filename(first->directory_path, "config.json", NULL);
    char *data = NULL;
    size_t length = 0;
    assert(papo_win_read_private(file, 1, &data, &length) == -1 && !data);
    assert(papo_win_read_private(file, 262144, &data, &length) == 1 && length > 0);
    free(data);
    profiles_destroy(first);
    profiles_destroy(stale);
    g_unlink(file);
    g_unlink(lockpath);
    char *directory = g_build_filename(root, "papo-tui", NULL);
    assert(g_rmdir(directory) == 0 && g_rmdir(root) == 0);
    g_free(directory);
    g_free(file);
    g_free(lockpath);
    g_free(root);
    curl_global_cleanup();
    return 0;
}

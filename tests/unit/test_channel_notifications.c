#include "config/channel_notifications.h"
#include <assert.h>
#include <curl/curl.h>
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <stdlib.h>
#include <string.h>

int main(void)
{
    assert(curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK);
    char *root = g_dir_make_tmp("papo-notifications-XXXXXX", NULL);
    assert(root);
    g_setenv("XDG_CONFIG_HOME", root, true);
    const char *url = "https://first.example.test/papo";
    const char *user = "00000000-0000-4000-8000-000000000001";
    const char *id = "00000000-0000-4000-8000-000000000010";
    assert(!channel_notifications_create("relative", user));
    assert(!channel_notifications_create(url, "invalid-account"));
    channel_notifications *local = channel_notifications_create(url, user);
    assert(local && !channel_notifications_mode(local, id)); /* Synced by default. */
    assert(!channel_notifications_set(local, "../bad", "off"));
    assert(!channel_notifications_set(local, id, "everything"));
    const char *modes[] = {"all", "only_mentions", "off"};
    for (size_t i = 0; i < G_N_ELEMENTS(modes); ++i) {
        assert(channel_notifications_set(local, id, modes[i]));
        channel_notifications_destroy(local);
        local = channel_notifications_create(url, user);
        assert(local && !strcmp(channel_notifications_mode(local, id), modes[i]));
    }
    channel_notifications *other =
        channel_notifications_create("https://second.example.test/papo", user);
    assert(other && !channel_notifications_mode(other, id));
    channel_notifications_destroy(other);
    other = channel_notifications_create(url, id);
    assert(other && !channel_notifications_mode(other, id));
    channel_notifications_destroy(other);
    other = channel_notifications_create(url, user);
    assert(!channel_notifications_refresh(other));
    assert(channel_notifications_set(local, id, "all"));
    /* Another instance sees the change and saves on top of it. */
    assert(channel_notifications_refresh(other) &&
           !strcmp(channel_notifications_mode(other, id), "all"));
    assert(channel_notifications_set(local, "00000000-0000-4000-8000-000000000011", "off"));
    assert(channel_notifications_set(other, id, "only_mentions"));
    assert(
        !strcmp(channel_notifications_mode(other, "00000000-0000-4000-8000-000000000011"), "off"));
    assert(channel_notifications_refresh(local) &&
           !strcmp(channel_notifications_mode(local, id), "only_mentions"));
    assert(channel_notifications_set(local, "00000000-0000-4000-8000-000000000011", NULL));
    channel_notifications_destroy(other);
    assert(channel_notifications_set(local, id, NULL));
    channel_notifications_destroy(local);
    local = channel_notifications_create(url, user);
    assert(local && !channel_notifications_mode(local, id));
    channel_notifications_destroy(local);
    char *directory = g_build_filename(root, "papo-tui", "channel-notifications", NULL);
    GDir *dir = g_dir_open(directory, 0, NULL);
    assert(dir);
    const char *name;
    while ((name = g_dir_read_name(dir))) {
        char *path = g_build_filename(directory, name, NULL);
        GStatBuf st;
        assert(!g_stat(path, &st));
#ifndef _WIN32
        assert((st.st_mode & 0777) == 0600);
#endif
        const char *invalid =
            "{\"version\":1,\"channels\":{\"00000000-0000-4000-8000-000000000010\":"
            "\"loud\"}}";
        assert(g_file_set_contents(path, invalid, -1, NULL));
        local = channel_notifications_create(url, user);
        assert(local && !channel_notifications_mode(local, id));
        assert(!channel_notifications_set(local, id, "off"));
        channel_notifications_destroy(local);
        char *data = NULL;
        assert(g_file_get_contents(path, &data, NULL, NULL) && !strcmp(data, invalid));
        g_free(data);
        assert(!g_remove(path));
        g_free(path);
    }
    g_dir_close(dir);
    assert(!g_rmdir(directory));
    g_free(directory);
    directory = g_build_filename(root, "papo-tui", NULL);
    assert(!g_rmdir(directory));
    g_free(directory);
    assert(!g_rmdir(root));
    g_free(root);
    puts("Device-only notification modes, restart, isolation, stale writers and invalid files "
         "passed");
    curl_global_cleanup();
    return 0;
}

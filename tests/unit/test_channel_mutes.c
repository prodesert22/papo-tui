#include "config/channel_mutes.h"
#include <assert.h>
#include <curl/curl.h>
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <jansson.h>
#include <stdlib.h>
#include <string.h>

int main(void)
{
    assert(curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK);
    char *root = g_dir_make_tmp("papo-mutes-XXXXXX", NULL);
    assert(root);
    g_setenv("XDG_CONFIG_HOME", root, true);
    const char *url = "https://first.example.test/papo";
    const char *user = "00000000-0000-4000-8000-000000000001";
    const char *id = "00000000-0000-4000-8000-000000000010";
    uint64_t now = 1800000000;
    assert(!channel_mutes_create("relative", user));
    assert(!channel_mutes_create("https://first.example.test/", user));
    assert(!channel_mutes_create(url, "invalid-account"));
    channel_mutes *m = channel_mutes_create(url, user);
    assert(m && !channel_mutes_active(m, id, now));
    channel_mutes *initial = channel_mutes_create(url, user);
    assert(initial);
    assert(channel_mutes_set(m, id, 600, now));
    assert(!channel_mutes_set(initial, id, 1800, now));
    channel_mutes_destroy(initial);
    const int durations[] = {600, 1800, 3600, 14400, 43200, 86400};
    for (size_t i = 0; i < G_N_ELEMENTS(durations); ++i) {
        assert(channel_mutes_set(m, id, durations[i], now));
        channel_mutes_destroy(m);
        m = channel_mutes_create(url, user);
        assert(channel_mutes_active(m, id, now + durations[i] - 1));
        assert(!channel_mutes_active(m, id, now + durations[i]));
        assert(channel_mutes_expire(m, now + durations[i]));
    }
    assert(!channel_mutes_set(m, "../bad", 600, now));
    assert(!channel_mutes_set(m, id, 1, now));
    assert(!channel_mutes_set(m, id, 600, UINT64_MAX));
    assert(channel_mutes_set(m, id, -1, now));
    assert(channel_mutes_active(m, id, now + 10 * 86400));
    channel_mutes *other = channel_mutes_create("https://second.example.test/papo", user);
    assert(other && !channel_mutes_active(other, id, now));
    channel_mutes_destroy(other);
    other = channel_mutes_create(url, id);
    assert(other && !channel_mutes_active(other, id, now));
    channel_mutes_destroy(other);
    other = channel_mutes_create(url, user);
    assert(other && channel_mutes_active(other, id, now));
    assert(channel_mutes_set(m, id, 600, now));
    assert(!channel_mutes_set(other, id, 1800, now)); /* Stale etag cannot overwrite. */
    channel_mutes_destroy(other);
    assert(channel_mutes_set(m, id, 0, now));
    channel_mutes_destroy(m);
    m = channel_mutes_create(url, user);
    assert(m && !channel_mutes_active(m, id, now));
    channel_mutes_destroy(m);
    char *directory = g_build_filename(root, "papo-tui", "channel-mutes", NULL);
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
        assert(g_file_set_contents(path, "{broken", -1, NULL));
        m = channel_mutes_create(url, user);
        assert(m && !channel_mutes_set(m, id, 600, now));
        channel_mutes_destroy(m);
        char *data = NULL;
        assert(g_file_get_contents(path, &data, NULL, NULL) && !strcmp(data, "{broken"));
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
    puts("Channel mute durations, expiry, restart, isolation, stale writers and malformed files "
         "passed");
    curl_global_cleanup();
    return 0;
}

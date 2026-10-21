#include "config/channel_mutes.h"
#include "model/conversation/history.h"
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <jansson.h>
#include <stdlib.h>
#include <string.h>

#define MUTE_BYTES (128U * 1024U)
struct channel_mutes {
    json_t *channels;
    GFile *file;
    char *etag;
    bool writable;
};
channel_mutes *channel_mutes_create(const char *url, const char *user)
{
    if (!url || !*url || strlen(url) > 2048 || !history_uuid(user))
        return NULL;
    char reason[192];
    char *canonical = auth_normalize_url(url, reason, sizeof(reason));
    bool scoped = canonical && !strcmp(canonical, url);
    curl_free(canonical);
    if (!scoped)
        return NULL;
    channel_mutes *m = calloc(1, sizeof(*m));
    if (!m)
        return NULL;
    m->channels = json_object();
    char *scope = g_strdup_printf("%s\n%s", url, user);
    char *digest = g_compute_checksum_for_string(G_CHECKSUM_SHA256, scope, -1);
    char *directory = g_build_filename(g_get_user_config_dir(), "papo-tui", "channel-mutes", NULL);
    char *name = g_strconcat(digest, ".json", NULL);
    char *path = g_build_filename(directory, name, NULL);
    m->file = g_file_new_for_path(path);
    m->writable = !g_mkdir_with_parents(directory, 0700);
    g_free(path);
    g_free(name);
    g_free(directory);
    g_free(digest);
    g_free(scope);
    if (!m->channels) {
        channel_mutes_destroy(m);
        return NULL;
    }
    GFileInfo *metadata = g_file_query_info(m->file, G_FILE_ATTRIBUTE_STANDARD_IS_SYMLINK,
                                            G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL, NULL);
    if (metadata) {
        if (g_file_info_get_is_symlink(metadata)) {
            m->writable = false;
            g_object_unref(metadata);
            return m;
        }
        g_object_unref(metadata);
    }
    GError *error = NULL;
    GFileInputStream *input = g_file_read(m->file, NULL, &error);
    if (!input) {
        m->writable &= g_error_matches(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
        g_clear_error(&error);
        return m;
    }
    GFileInfo *info =
        g_file_input_stream_query_info(input, G_FILE_ATTRIBUTE_ETAG_VALUE, NULL, NULL);
    if (info) {
        m->etag = g_strdup(g_file_info_get_etag(info));
        g_object_unref(info);
    }
    char *data = malloc(MUTE_BYTES + 1);
    gsize size = 0;
    bool ok =
        data &&
        g_input_stream_read_all(G_INPUT_STREAM(input), data, MUTE_BYTES + 1, &size, NULL, NULL) &&
        size <= MUTE_BYTES;
    json_t *root = ok ? json_loadb(data, size, JSON_REJECT_DUPLICATES, NULL) : NULL;
    free(data);
    g_object_unref(input);
    json_t *channels = json_object_get(root, "channels");
    ok = json_is_object(root) && json_object_size(root) == 2 &&
         json_is_integer(json_object_get(root, "version")) &&
         json_integer_value(json_object_get(root, "version")) == 1 && json_is_object(channels) &&
         json_object_size(channels) <= HISTORY_CHANNEL_LIMIT;
    const char *id;
    json_t *expires;
    json_object_foreach(channels, id, expires) if (!history_uuid(id) || !json_is_integer(expires) ||
                                                   (json_integer_value(expires) != -1 &&
                                                    json_integer_value(expires) <= 0)) ok = false;
    if (ok) {
        json_decref(m->channels);
        m->channels = json_incref(channels);
    }
    m->writable &= ok && m->etag != NULL;
    json_decref(root);
    return m;
}
void channel_mutes_destroy(channel_mutes *m)
{
    if (!m)
        return;
    json_decref(m->channels);
    g_clear_object(&m->file);
    g_free(m->etag);
    free(m);
}
bool channel_mutes_active(const channel_mutes *m, const char *id, uint64_t now)
{
    json_t *entry = m && id ? json_object_get(m->channels, id) : NULL;
    json_int_t expires = json_integer_value(entry);
    return entry && (expires == -1 || (expires > 0 && (uint64_t)expires > now));
}
bool channel_mutes_expire(channel_mutes *m, uint64_t now)
{
    if (!m)
        return false;
    bool changed = false;
    const char *id;
    json_t *expires;
    void *next;
    json_object_foreach_safe(m->channels, next, id,
                             expires) if (json_integer_value(expires) > 0 &&
                                          (uint64_t)json_integer_value(expires) <= now)
    {
        json_object_del(m->channels, id);
        changed = true;
    }
    return changed;
}
bool channel_mutes_set(channel_mutes *m, const char *id, int seconds, uint64_t now)
{
    if (!m || !m->writable || !history_uuid(id) ||
        (seconds != -1 && seconds != 0 && seconds != 600 && seconds != 1800 && seconds != 3600 &&
         seconds != 14400 && seconds != 43200 && seconds != 86400) ||
        now > INT64_MAX - 86400U)
        return false;
    json_t *copy = json_deep_copy(m->channels);
    if (!copy)
        return false;
    if (!seconds)
        json_object_del(copy, id);
    bool ok = !seconds || !json_object_set_new(
                              copy, id, json_integer(seconds < 0 ? -1 : (json_int_t)now + seconds));
    ok &= json_object_size(copy) <= HISTORY_CHANNEL_LIMIT;
    json_t *root = ok ? json_pack("{s:i,s:O}", "version", 1, "channels", copy) : NULL;
    char *data = root ? json_dumps(root, JSON_COMPACT | JSON_SORT_KEYS) : NULL;
    char *etag = NULL;
    ok = data && strlen(data) <= MUTE_BYTES &&
         /* A nonmatching expected tag also protects a previously absent file: a
          * first save from another process must not become an unconditional overwrite. */
         g_file_replace_contents(
             m->file, data, strlen(data), m->etag ? m->etag : "papo-channel-mutes-absent", false,
             G_FILE_CREATE_PRIVATE | G_FILE_CREATE_REPLACE_DESTINATION, &etag, NULL, NULL);
    if (ok) {
        json_decref(m->channels);
        m->channels = json_incref(copy);
        g_free(m->etag);
        m->etag = etag;
    } else
        g_free(etag);
    free(data);
    json_decref(root);
    json_decref(copy);
    return ok;
}

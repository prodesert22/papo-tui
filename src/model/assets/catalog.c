#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "model/assets/catalog.h"
#include "config/app_mode.h"
#include "model/conversation/history.h"
#include "net/auth/session_time.h"
#include "net/clock.h"
#include "platform/runtime.h"
#include <curl/curl.h>
#include <errno.h>
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#define EMOJI_COUNT 500
#define EMOJI_BYTES (2U * 1024U * 1024U)
#define AVATAR_LIMIT 10000U
#define APP_AVATAR_COUNT 32
#define TEMP_BYTES (160U * 1024U * 1024U)
typedef struct {
    author_avatar image;
    char url[2049];
} app_avatar;
struct chat_assets {
    server_emoji emojis[EMOJI_COUNT];
    GHashTable *avatars;        /* Owns stable per-user entries for this session. */
    GPtrArray *visible_avatars; /* Borrows entries visited in the current frame. */
    app_avatar apps[APP_AVATAR_COUNT];
    size_t count, bytes;
    uint64_t revision, emoji_revision, epoch, refreshed, tick, visible_tick;
    bool wanted, loaded, failed, refreshing;
    char directory[4096], cursor[64], last_id[37], endpoint[320], notice[160];
    json_t *profiles;
    uint64_t profile_versions[4];
};
static const char *str(const json_t *o, const char *key)
{
    const char *s = json_string_value(json_object_get(o, key));
    return s ? s : "";
}
static bool valid_string(const json_t *o, const char *key, size_t limit)
{
    json_t *v = json_object_get(o, key);
    return json_is_string(v) && json_string_length(v) <= limit &&
           strlen(json_string_value(v)) == json_string_length(v);
}
static void remove_file(chat_assets *a, char **path, size_t *bytes)
{
    if (*path) {
        unlink(*path);
        free(*path);
        *path = NULL;
    }
    a->bytes -= *bytes;
    *bytes = 0;
}
static void avatar_free(gpointer value)
{
    author_avatar *p = value;
    if (p->path) {
        unlink(p->path);
        free(p->path);
    }
    free(p);
}
static uint64_t fingerprint(const char *value)
{
    uint64_t n = UINT64_C(14695981039346656037);
    size_t length = strlen(value);
    for (size_t i = 0; i < length; ++i) {
        n ^= (unsigned char)value[i];
        n *= UINT64_C(1099511628211);
    }
    return n ? n : 1;
}
static size_t temporary_available(const chat_assets *a)
{
    return a->bytes <= TEMP_BYTES ? TEMP_BYTES - a->bytes : 0;
}
static char *blob_file(chat_assets *a, const char *blob, size_t limit, size_t *size)
{
    *size = 0;
    size_t length = strlen(blob);
    if (!length || length > (limit + 2) / 3 * 4 || length % 4)
        return NULL;
    size_t padding = (blob[length - 1] == '=') + (blob[length - 2] == '=');
    for (size_t i = 0; i < length - padding; ++i)
        if (!g_ascii_isalnum(blob[i]) && blob[i] != '+' && blob[i] != '/')
            return NULL;
    gsize bytes = 0;
    unsigned char *data = g_base64_decode(blob, &bytes);
    if (!data)
        return NULL;
    if (bytes <= 0 || (size_t)bytes > limit || (size_t)bytes > temporary_available(a)) {
        g_free(data);
        return NULL;
    }
    char pattern[sizeof(a->directory) + sizeof("/blob-XXXXXX")];
    snprintf(pattern, sizeof(pattern), "%s/blob-XXXXXX", a->directory);
    int fd = papo_mkstemp(pattern);
    if (fd < 0) {
        g_free(data);
        return NULL;
    }
    size_t written = 0;
    while (written < (size_t)bytes) {
        ssize_t n = write(fd, data + written, (size_t)bytes - written);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            break;
        written += (size_t)n;
    }
    g_free(data);
    bool ok = close(fd) == 0 && written == (size_t)bytes;
    char *path = ok ? strdup(pattern) : NULL;
    if (!path) {
        unlink(pattern);
        return NULL;
    }
    *size = (size_t)bytes;
    a->bytes += *size;
    return path;
}
chat_assets *chat_assets_create(void)
{
    chat_assets *a = calloc(1, sizeof(*a));
    if (!a)
        return NULL;
    snprintf(a->directory, sizeof(a->directory), "%s/papo-assets-XXXXXX", g_get_tmp_dir());
    if (!papo_mkdtemp(a->directory)) {
        free(a);
        return NULL;
    }
    a->avatars = g_hash_table_new_full(g_str_hash, g_str_equal, NULL, avatar_free);
    a->visible_avatars = g_ptr_array_new();
    a->visible_tick = 1;
    return a;
}
void chat_assets_destroy(chat_assets *a)
{
    if (!a)
        return;
    for (size_t i = 0; i < a->count; ++i)
        remove_file(a, &a->emojis[i].path, &a->emojis[i].bytes);
    g_ptr_array_free(a->visible_avatars, true);
    g_hash_table_destroy(a->avatars);
    for (size_t i = 0; i < APP_AVATAR_COUNT; ++i)
        remove_file(a, &a->apps[i].image.path, &a->apps[i].image.bytes);
    json_decref(a->profiles);
    rmdir(a->directory);
    free(a);
}
void chat_assets_emojis_request(chat_assets *a, bool refresh)
{
    uint64_t now = 0;
    net_now_ms(&now);
    if (app_mode_get() == APP_MODE_ULTRALIGHT)
        return;
    if (!a || a->wanted || (!refresh && (a->failed || (a->loaded && now - a->refreshed < 300000))))
        return;
    a->wanted = true;
    a->failed = false;
    a->refreshing = true;
    ++a->epoch;
    a->cursor[0] = a->last_id[0] = 0;
    snprintf(a->notice, sizeof(a->notice), "Carregando emojis…");
    ++a->revision;
}
bool chat_assets_emojis_wanted(const chat_assets *a)
{
    return a && a->wanted;
}
const char *chat_assets_emojis_path(chat_assets *a)
{
    char *cursor = curl_easy_escape(NULL, a->cursor, 0);
    if (!cursor)
        return NULL;
    snprintf(a->endpoint, sizeof(a->endpoint), "/emojis?order=asc%s%s%s%s",
             *a->cursor ? "&since=" : "", cursor, *a->last_id ? "&last_id=" : "", a->last_id);
    curl_free(cursor);
    return a->endpoint;
}
static void emoji_drop(chat_assets *a, size_t i)
{
    remove_file(a, &a->emojis[i].path, &a->emojis[i].bytes);
    memmove(a->emojis + i, a->emojis + i + 1, (a->count - i - 1) * sizeof(*a->emojis));
    --a->count;
}
bool chat_assets_emojis_response(chat_assets *a, json_t *response, long status)
{
    if (app_mode_get() == APP_MODE_ULTRALIGHT)
        return false;
    if (!a)
        return false;
    json_t *items = json_object_get(response, "emojis"),
           *more = json_object_get(response, "has_more");
    bool ok = status == 200 && json_is_array(items) && json_array_size(items) <= 25 &&
              json_is_boolean(more);
    uint64_t stamp;
    size_t i;
    json_t *e;
    if (ok)
        json_array_foreach(items, i, e)
        {
            if (!history_uuid(str(e, "id")) || !valid_string(e, "name", 128) || !*str(e, "name") ||
                !valid_string(e, "created_at", 63) ||
                !session_time_parse(str(e, "created_at"), &stamp) ||
                !valid_string(e, "format", 7) ||
                !valid_string(e, "image_blob", (EMOJI_BYTES + 2U) / 3U * 4U)) {
                ok = false;
                break;
            }
            /* URI encoding below makes ISO offsets with '+' safe in query parameters. */
            size_t n = 0;
            while (n < a->count && strcmp(a->emojis[n].id, str(e, "id")))
                ++n;
            if (n == a->count) {
                if (a->count == EMOJI_COUNT) {
                    ok = false;
                    break;
                }
                memset(&a->emojis[a->count++], 0, sizeof(*a->emojis));
            }
            server_emoji *entry = &a->emojis[n];
            uint64_t version = fingerprint(str(e, "image_blob"));
            if (entry->revision != version || !entry->path) {
                remove_file(a, &entry->path, &entry->bytes);
                entry->path = blob_file(a, str(e, "image_blob"), EMOJI_BYTES, &entry->bytes);
                entry->revision = version;
            }
            snprintf(entry->id, sizeof(entry->id), "%s", str(e, "id"));
            snprintf(entry->name, sizeof(entry->name), "%s", str(e, "name"));
            snprintf(entry->format, sizeof(entry->format), "%s", str(e, "format"));
            snprintf(entry->created, sizeof(entry->created), "%s", str(e, "created_at"));
            entry->seen = a->epoch;
        }
    if (ok && json_is_true(more)) {
        json_t *last = json_array_get(items, json_array_size(items) - 1);
        if (!last ||
            (!strcmp(a->last_id, str(last, "id")) && !strcmp(a->cursor, str(last, "created_at"))))
            ok = false;
        else {
            snprintf(a->cursor, sizeof(a->cursor), "%s", str(last, "created_at"));
            snprintf(a->last_id, sizeof(a->last_id), "%s", str(last, "id"));
        }
    } else if (ok) {
        for (size_t n = 0; n < a->count;) {
            if (a->emojis[n].seen != a->epoch)
                emoji_drop(a, n);
            else
                ++n;
        }
        a->wanted = a->refreshing = false;
        a->loaded = true;
        net_now_ms(&a->refreshed);
        snprintf(a->notice, sizeof(a->notice), "%zu emojis do servidor · F5 atualizar", a->count);
    }
    if (!ok) {
        a->wanted = false;
        a->failed = true;
        snprintf(a->notice, sizeof(a->notice),
                 status == 429 ? "Limite atingido · Aguarde e use F5"
                               : "Falha nos emojis · F5 tentar novamente");
    }
    ++a->revision;
    ++a->emoji_revision;
    return ok;
}
size_t chat_assets_emoji_count(const chat_assets *a)
{
    return a ? a->count : 0;
}
const server_emoji *chat_assets_emoji(const chat_assets *a, size_t i)
{
    return a && i < a->count ? &a->emojis[i] : NULL;
}
const server_emoji *chat_assets_emoji_id(const chat_assets *a, const char *id)
{
    if (!id)
        return NULL;
    for (size_t i = 0; a && i < a->count; ++i)
        if (!strcmp(a->emojis[i].id, id))
            return &a->emojis[i];
    return NULL;
}
const char *chat_assets_notice(const chat_assets *a)
{
    return a ? a->notice : "Emojis indisponíveis";
}
uint64_t chat_assets_revision(const chat_assets *a)
{
    return a ? a->revision : 0;
}
uint64_t chat_assets_emoji_revision(const chat_assets *a)
{
    return a ? a->emoji_revision : 0;
}
const author_avatar *chat_assets_avatar(chat_assets *a, const char *id)
{
    if (!a || !history_uuid(id))
        return NULL;
    author_avatar *p = g_hash_table_lookup(a->avatars, id);
    if (!p) {
        if (g_hash_table_size(a->avatars) >= AVATAR_LIMIT)
            return NULL;
        p = calloc(1, sizeof(*p));
        if (!p)
            return NULL;
        snprintf(p->id, sizeof(p->id), "%s", id);
        p->dirty = true;
        p->version = 1;
        g_hash_table_insert(a->avatars, p->id, p);
    }
    if (p->used < a->visible_tick)
        g_ptr_array_add(a->visible_avatars, p);
    p->used = ++a->tick;
    return p;
}
const author_avatar *chat_assets_webhook_avatar(chat_assets *a, const json_t *message)
{
    if (!a || !history_message_webhook(message))
        return NULL;
    json_t *external = json_object_get(message, "external_author");
    if (!valid_string(external, "avatar_url", 2048))
        return NULL;
    const char *url = str(external, "avatar_url");
    if (!*url)
        return NULL;
    app_avatar *slot = NULL;
    for (size_t i = 0; i < APP_AVATAR_COUNT; ++i) {
        app_avatar *p = &a->apps[i];
        if (!strcmp(p->url, url)) {
            p->image.used = ++a->tick;
            return &p->image;
        }
        if (!slot || p->image.used < slot->image.used)
            slot = p;
    }
    if (!slot)
        return NULL;
    remove_file(a, &slot->image.path, &slot->image.bytes);
    memset(slot, 0, sizeof(*slot));
    snprintf(slot->url, sizeof(slot->url), "%s", url);
    snprintf(slot->image.id, sizeof(slot->image.id), "%016llx",
             (unsigned long long)fingerprint(url));
    slot->image.dirty = true;
    slot->image.used = ++a->tick;
    slot->image.revision = 1;
    slot->image.url = slot->url;
    ++a->revision;
    return &slot->image;
}
void chat_assets_avatar_invalidate(chat_assets *a, const char *id)
{
    if (!a || !history_uuid(id))
        return;
    author_avatar *p = g_hash_table_lookup(a->avatars, id);
    if (p) {
        /* Keep the last good image until the new snapshot is validated. */
        ++p->version;
        p->dirty = true;
        p->failed = false;
        ++a->revision;
    }
}
bool chat_assets_profiles_wanted(const chat_assets *a)
{
    if (app_mode_get() == APP_MODE_ULTRALIGHT)
        return false;
    for (size_t i = 0; a && i < a->visible_avatars->len; ++i) {
        author_avatar *p = g_ptr_array_index(a->visible_avatars, i);
        if (p->dirty && !p->requested)
            return true;
    }
    return false;
}
json_t *chat_assets_profiles_request(chat_assets *a)
{
    if (!a)
        return NULL;
    if (a->profiles)
        return a->profiles;
    a->profiles = json_array();
    if (!a->profiles)
        return NULL;
    for (size_t i = 0; i < a->visible_avatars->len && json_array_size(a->profiles) < 4; ++i) {
        author_avatar *p = g_ptr_array_index(a->visible_avatars, i);
        if (!p->dirty || p->requested)
            continue;
        size_t n = json_array_size(a->profiles);
        if (json_array_append_new(a->profiles, json_string(p->id)))
            break;
        a->profile_versions[n] = p->version;
        p->requested = true;
        p->dirty = false;
    }
    return a->profiles;
}
static void store_profile_avatar(chat_assets *a, author_avatar *p, const json_t *found)
{
    p->dirty = p->requested = p->failed = false;
    json_t *blob = json_object_get(found, "avatar_blob");
    size_t length = json_string_length(blob);
    if (length && length <= 2796204 && valid_string(found, "avatar_blob", 2796204)) {
        char *checksum =
            g_compute_checksum_for_string(G_CHECKSUM_SHA256, str(found, "avatar_blob"), -1);
        if (!checksum) {
            p->failed = true;
            return;
        }
        if (p->path && !strcmp(checksum, p->checksum)) {
            g_free(checksum);
            return;
        }
        /* Compact inputs share the existing asset file budget. Evicting
         * visible avatars from a smaller private quota caused repeated
         * profile reads despite the shared decoded-image cache. */
        while (length / 4 * 3 > temporary_available(a)) {
            author_avatar *victim = NULL;
            GHashTableIter iter;
            gpointer value;
            g_hash_table_iter_init(&iter, a->avatars);
            while (g_hash_table_iter_next(&iter, NULL, &value)) {
                author_avatar *v = value;
                if (v != p && v->path && !v->requested && v->used < a->visible_tick &&
                    (!victim || v->used < victim->used))
                    victim = v;
            }
            if (!victim)
                break;
            remove_file(a, &victim->path, &victim->bytes);
            /* Retry only when the evicted author becomes visible again. */
            victim->dirty = true;
            ++victim->revision;
        }
        size_t bytes = 0;
        char *path = blob_file(a, str(found, "avatar_blob"), 2U * 1024U * 1024U, &bytes);
        if (path) {
            remove_file(a, &p->path, &p->bytes);
            p->path = path;
            p->bytes = bytes;
            snprintf(p->checksum, sizeof(p->checksum), "%s", checksum);
            ++p->revision;
        } else
            p->failed = true;
        g_free(checksum);
    } else if ((!blob || json_is_null(blob) || (json_is_string(blob) && !length))) {
        if (p->path) {
            remove_file(a, &p->path, &p->bytes);
            p->checksum[0] = 0;
            ++p->revision;
        }
    } else {
        p->failed = true;
    }
}
bool chat_assets_profiles_response(chat_assets *a, json_t *response, long status)
{
    if (app_mode_get() == APP_MODE_ULTRALIGHT) {
        chat_assets_cancel(a);
        return false;
    }
    if (!a)
        return false;
    json_t *profiles = json_object_get(response, "profiles");
    bool ok = status == 200 && json_is_array(profiles) && json_array_size(profiles) <= 4;
    size_t i;
    json_t *id;
    json_array_foreach(a->profiles, i, id)
    {
        author_avatar *p = g_hash_table_lookup(a->avatars, json_string_value(id));
        if (!p)
            continue;
        p->requested = false;
        if (p->version != a->profile_versions[i])
            continue;
        p->failed = !ok;
        if (!ok)
            continue;
        json_t *found = NULL, *profile;
        size_t k;
        if (ok)
            json_array_foreach(profiles, k, profile) if (!strcmp(str(profile, "id"), p->id))
            {
                found = profile;
                break;
            }
        store_profile_avatar(a, p, found);
    }
    json_decref(a->profiles);
    a->profiles = NULL;
    ++a->revision;
    return ok;
}
void chat_assets_cancel(chat_assets *a)
{
    if (!a)
        return;
    size_t i;
    json_t *id;
    json_array_foreach(a->profiles, i, id)
    {
        author_avatar *p = g_hash_table_lookup(a->avatars, json_string_value(id));
        if (p) {
            p->requested = false;
            p->dirty = true;
        }
    }
    json_decref(a->profiles);
    a->profiles = NULL;
}

void chat_assets_visible_begin(chat_assets *a)
{
    if (a) {
        a->visible_tick = a->tick + 1;
        g_ptr_array_set_size(a->visible_avatars, 0);
    }
}
void chat_assets_profiles_retry(chat_assets *a)
{
    for (size_t i = 0; a && i < APP_AVATAR_COUNT; ++i)
        if (*a->apps[i].url) {
            ++a->apps[i].image.revision;
            ++a->revision;
        }
    if (!a)
        return;
    GHashTableIter iter;
    gpointer value;
    g_hash_table_iter_init(&iter, a->avatars);
    while (g_hash_table_iter_next(&iter, NULL, &value)) {
        author_avatar *p = value;
        if ((!p->path || p->failed) && !p->requested)
            p->dirty = true;
    }
}

void chat_assets_clear_media(chat_assets *a)
{
    if (!a)
        return;
    chat_assets_cancel(a);
    g_ptr_array_set_size(a->visible_avatars, 0);
    GHashTableIter it;
    gpointer value;
    g_hash_table_iter_init(&it, a->avatars);
    while (g_hash_table_iter_next(&it, NULL, &value)) {
        author_avatar *p = value;
        remove_file(a, &p->path, &p->bytes);
    }
    g_hash_table_remove_all(a->avatars);
    for (size_t i = 0; i < a->count; ++i)
        remove_file(a, &a->emojis[i].path, &a->emojis[i].bytes);
    for (size_t i = 0; i < APP_AVATAR_COUNT; ++i) {
        remove_file(a, &a->apps[i].image.path, &a->apps[i].image.bytes);
        memset(&a->apps[i], 0, sizeof(a->apps[i]));
    }
    a->wanted = a->loaded = a->failed = a->refreshing = false;
    ++a->revision;
    ++a->emoji_revision;
    snprintf(a->notice, sizeof(a->notice), "Emojis gráficos desativados no modo ultralight.");
}

void chat_assets_profile_observe(chat_assets *a, const json_t *profile, uint64_t version)
{
    if (!a || app_mode_get() == APP_MODE_ULTRALIGHT)
        return;
    const char *id = str(profile, "id");
    if (!chat_assets_avatar(a, id))
        return;
    author_avatar *p = g_hash_table_lookup(a->avatars, id);
    if (p->version != version)
        return;
    store_profile_avatar(a, p, profile);
    ++a->revision;
}

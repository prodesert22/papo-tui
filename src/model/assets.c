#define _POSIX_C_SOURCE 200809L
#include "model/assets.h"
#include "model/history.h"
#include "net/clock.h"
#include "net/session_time.h"
#include <libavutil/base64.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <curl/curl.h>
#define EMOJI_COUNT 500
#define AVATAR_COUNT 128
#define TEMP_BYTES (160U * 1024U * 1024U)
#define AVATAR_BYTES (8U * 1024U * 1024U)
struct chat_assets {
    server_emoji emojis[EMOJI_COUNT];
    author_avatar avatars[AVATAR_COUNT];
    size_t count, bytes, avatar_bytes;
    uint64_t revision, epoch, refreshed, tick, visible_tick;
    bool wanted, loaded, failed, refreshing;
    char directory[128], cursor[64], last_id[37], endpoint[320], notice[160];
    json_t *profiles;
    uint64_t profile_versions[4];
};
static const char *str(json_t *o, const char *key)
{
    const char *s = json_string_value(json_object_get(o, key)); return s ? s : "";
}
static bool valid_string(json_t *o, const char *key, size_t limit)
{
    json_t *v = json_object_get(o, key);
    return json_is_string(v) && json_string_length(v) <= limit && strlen(json_string_value(v)) == json_string_length(v);
}
static void remove_file(chat_assets *a, char **path, size_t *bytes)
{
    if (*path) { unlink(*path); free(*path); *path = NULL; }
    a->bytes -= *bytes; *bytes = 0;
}
static uint64_t fingerprint(const char *value)
{
    uint64_t n = UINT64_C(14695981039346656037);
    for (const unsigned char *p = (const unsigned char *)value; *p; ++p) { n ^= *p; n *= UINT64_C(1099511628211); }
    return n ? n : 1;
}
static char *blob_file(chat_assets *a, const char *blob, size_t limit, size_t *size)
{
    *size = 0;
    size_t length = strlen(blob);
    if (!length || length > (limit + 2) / 3 * 4 || length % 4) return NULL;
    unsigned char *data = malloc(limit + 1);
    if (!data) return NULL;
    int bytes = av_base64_decode(data, blob, (int)limit + 1);
    if (bytes <= 0 || (size_t)bytes > limit || (size_t)bytes > TEMP_BYTES - a->bytes) { free(data); return NULL; }
    char pattern[160]; snprintf(pattern, sizeof(pattern), "%s/blob-XXXXXX", a->directory);
    int fd = mkstemp(pattern);
    if (fd < 0) { free(data); return NULL; }
    size_t written = 0;
    while (written < (size_t)bytes) {
        ssize_t n = write(fd, data + written, (size_t)bytes - written);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        written += (size_t)n;
    }
    free(data);
    bool ok = close(fd) == 0 && written == (size_t)bytes;
    char *path = ok ? strdup(pattern) : NULL;
    if (!path) { unlink(pattern); return NULL; }
    *size = (size_t)bytes; a->bytes += *size; return path;
}
chat_assets *chat_assets_create(void)
{
    chat_assets *a = calloc(1, sizeof(*a));
    if (!a) return NULL;
    snprintf(a->directory, sizeof(a->directory), "/tmp/papo-assets-XXXXXX");
    if (!mkdtemp(a->directory)) { free(a); return NULL; }
    return a;
}
void chat_assets_destroy(chat_assets *a)
{
    if (!a) return;
    for (size_t i = 0; i < a->count; ++i) remove_file(a, &a->emojis[i].path, &a->emojis[i].bytes);
    for (size_t i = 0; i < AVATAR_COUNT; ++i) remove_file(a, &a->avatars[i].path, &a->avatars[i].bytes);
    json_decref(a->profiles); rmdir(a->directory); free(a);
}
void chat_assets_emojis_request(chat_assets *a, bool refresh)
{
    uint64_t now = 0; net_now_ms(&now);
    if (!a || a->wanted || (!refresh && (a->failed || (a->loaded && now - a->refreshed < 300000)))) return;
    a->wanted = true; a->failed = false; a->refreshing = true; ++a->epoch;
    a->cursor[0] = a->last_id[0] = 0;
    snprintf(a->notice, sizeof(a->notice), "Carregando emojis…"); ++a->revision;
}
bool chat_assets_emojis_wanted(const chat_assets *a) { return a && a->wanted; }
const char *chat_assets_emojis_path(chat_assets *a)
{
    char *cursor = curl_easy_escape(NULL, a->cursor, 0);
    if (!cursor) return NULL;
    snprintf(a->endpoint, sizeof(a->endpoint), "/emojis?order=asc%s%s%s%s", *a->cursor ? "&since=" : "", cursor,
             *a->last_id ? "&last_id=" : "", a->last_id);
    curl_free(cursor);
    return a->endpoint;
}
static void emoji_drop(chat_assets *a, size_t i)
{
    remove_file(a, &a->emojis[i].path, &a->emojis[i].bytes);
    memmove(a->emojis + i, a->emojis + i + 1, (a->count - i - 1) * sizeof(*a->emojis)); --a->count;
}
bool chat_assets_emojis_response(chat_assets *a, json_t *response, long status)
{
    if (!a) return false;
    json_t *items = json_object_get(response, "emojis"), *more = json_object_get(response, "has_more");
    bool ok = status == 200 && json_is_array(items) && json_array_size(items) <= 25 && json_is_boolean(more);
    uint64_t stamp;
    size_t i; json_t *e;
    if (ok) json_array_foreach(items, i, e) {
        if (!history_uuid(str(e, "id")) || !valid_string(e, "name", 128) || !*str(e, "name") ||
            !valid_string(e, "created_at", 63) || !session_time_parse(str(e, "created_at"), &stamp) ||
            !valid_string(e, "format", 7) || !valid_string(e, "image_blob", 349528)) { ok = false; break; }
        /* URI encoding below makes ISO offsets with '+' safe in query parameters. */
        size_t n = 0;
        while (n < a->count && strcmp(a->emojis[n].id, str(e, "id"))) ++n;
        if (n == a->count) {
            if (a->count == EMOJI_COUNT) { ok = false; break; }
            memset(&a->emojis[a->count++], 0, sizeof(*a->emojis));
        }
        server_emoji *entry = &a->emojis[n];
        uint64_t version = fingerprint(str(e, "image_blob"));
        if (entry->revision != version || !entry->path) {
            remove_file(a, &entry->path, &entry->bytes);
            entry->path = blob_file(a, str(e, "image_blob"), 256U * 1024U, &entry->bytes);
            entry->revision = version;
        }
        snprintf(entry->id, sizeof(entry->id), "%s", str(e, "id"));
        snprintf(entry->name, sizeof(entry->name), "%s", str(e, "name"));
        snprintf(entry->format, sizeof(entry->format), "%s", str(e, "format"));
        snprintf(entry->created, sizeof(entry->created), "%s", str(e, "created_at")); entry->seen = a->epoch;
    }
    if (ok && json_is_true(more)) {
        json_t *last = json_array_get(items, json_array_size(items) - 1);
        if (!last || (!strcmp(a->last_id, str(last, "id")) && !strcmp(a->cursor, str(last, "created_at")))) ok = false;
        else { snprintf(a->cursor, sizeof(a->cursor), "%s", str(last, "created_at")); snprintf(a->last_id, sizeof(a->last_id), "%s", str(last, "id")); }
    } else if (ok) {
        for (size_t n = 0; n < a->count;) { if (a->emojis[n].seen != a->epoch) emoji_drop(a, n); else ++n; }
        a->wanted = a->refreshing = false; a->loaded = true; net_now_ms(&a->refreshed);
        snprintf(a->notice, sizeof(a->notice), "%zu emojis do servidor · F5 atualizar", a->count);
    }
    if (!ok) { a->wanted = false; a->failed = true; snprintf(a->notice, sizeof(a->notice), status == 429 ? "Limite atingido · Aguarde e use F5" : "Falha nos emojis · F5 tentar novamente"); }
    ++a->revision; return ok;
}
size_t chat_assets_emoji_count(const chat_assets *a) { return a ? a->count : 0; }
const server_emoji *chat_assets_emoji(const chat_assets *a, size_t i) { return a && i < a->count ? &a->emojis[i] : NULL; }
const server_emoji *chat_assets_emoji_id(const chat_assets *a, const char *id)
{
    if (!id) return NULL;
    for (size_t i = 0; a && i < a->count; ++i) if (!strcmp(a->emojis[i].id, id)) return &a->emojis[i];
    return NULL;
}
const char *chat_assets_notice(const chat_assets *a) { return a ? a->notice : "Emojis indisponíveis"; }
uint64_t chat_assets_revision(const chat_assets *a) { return a ? a->revision : 0; }
const author_avatar *chat_assets_avatar(chat_assets *a, const char *id)
{
    if (!a || !history_uuid(id)) return NULL;
    author_avatar *slot = NULL;
    for (size_t i = 0; i < AVATAR_COUNT; ++i) {
        author_avatar *p = &a->avatars[i];
        if (!strcmp(p->id, id)) { p->used = ++a->tick; return p; }
        if (!*p->id) slot = p;
    }
    if (!slot) {
        for (size_t i = 0; i < AVATAR_COUNT; ++i) {
            author_avatar *p = &a->avatars[i];
            if (!p->requested && (!slot || p->used < slot->used)) slot = p;
        }
    }
    if (!slot) return NULL;
    a->avatar_bytes -= slot->bytes; remove_file(a, &slot->path, &slot->bytes);
    memset(slot, 0, sizeof(*slot)); snprintf(slot->id, sizeof(slot->id), "%s", id);
    slot->dirty = true; slot->used = ++a->tick; return slot;
}
void chat_assets_avatar_invalidate(chat_assets *a, const char *id)
{
    if (!a || !history_uuid(id)) return;
    for (size_t i = 0; a && i < AVATAR_COUNT; ++i) if (!strcmp(a->avatars[i].id, id)) {
        author_avatar *p = &a->avatars[i]; a->avatar_bytes -= p->bytes; remove_file(a, &p->path, &p->bytes);
        ++p->revision; p->dirty = true; ++a->revision;
    }
}
bool chat_assets_profiles_wanted(const chat_assets *a)
{
    for (size_t i = 0; a && i < AVATAR_COUNT; ++i) if (a->avatars[i].dirty && !a->avatars[i].requested && a->avatars[i].used >= a->visible_tick) return true;
    return false;
}
json_t *chat_assets_profiles_request(chat_assets *a)
{
    if (!a) return NULL;
    if (a->profiles) return a->profiles;
    a->profiles = json_array();
    if (!a->profiles) return NULL;
    for (size_t i = 0; i < AVATAR_COUNT && json_array_size(a->profiles) < 4; ++i) {
        author_avatar *p = &a->avatars[i];
        if (!p->dirty || p->requested || p->used < a->visible_tick) continue;
        size_t n = json_array_size(a->profiles);
        if (json_array_append_new(a->profiles, json_string(p->id))) break;
        a->profile_versions[n] = p->revision; p->requested = true; p->dirty = false;
    }
    return a->profiles;
}
bool chat_assets_profiles_response(chat_assets *a, json_t *response, long status)
{
    if (!a) return false;
    json_t *profiles = json_object_get(response, "profiles");
    bool ok = status == 200 && json_is_array(profiles) && json_array_size(profiles) <= 4;
    size_t i; json_t *id;
    json_array_foreach(a->profiles, i, id) {
        author_avatar *p = (author_avatar *)chat_assets_avatar(a, json_string_value(id));
        if (!p) continue;
        p->requested = false;
        if (p->revision != a->profile_versions[i]) continue;
        json_t *found = NULL, *profile; size_t k;
        if (ok) json_array_foreach(profiles, k, profile) if (!strcmp(str(profile, "id"), p->id)) { found = profile; break; }
        size_t length = json_string_length(json_object_get(found, "avatar_blob"));
        if (length && length <= 2796204 && valid_string(found, "avatar_blob", 2796204)) {
            while (a->avatar_bytes + (length / 4 * 3) > AVATAR_BYTES) {
                author_avatar *victim = NULL;
                for (size_t j = 0; j < AVATAR_COUNT; ++j) {
                    author_avatar *v = &a->avatars[j];
                    if (v != p && v->path && (!victim || v->used < victim->used)) victim = v;
                }
                if (!victim) break;
                a->avatar_bytes -= victim->bytes; remove_file(a, &victim->path, &victim->bytes);
                /* Retry only when the evicted author becomes visible again. */
                victim->id[0] = 0;
            }
            a->avatar_bytes -= p->bytes; remove_file(a, &p->path, &p->bytes);
            p->path = blob_file(a, str(found, "avatar_blob"), 2U * 1024U * 1024U, &p->bytes);
            a->avatar_bytes += p->bytes;
        }
        ++p->revision;
    }
    json_decref(a->profiles); a->profiles = NULL; ++a->revision; return ok;
}
void chat_assets_cancel(chat_assets *a)
{
    if (!a) return;
    for (size_t i = 0; i < AVATAR_COUNT; ++i) if (a->avatars[i].requested) {
        a->avatars[i].requested = false; a->avatars[i].dirty = true;
    }
    json_decref(a->profiles); a->profiles = NULL;
}

void chat_assets_visible_begin(chat_assets *a) { if (a) a->visible_tick = a->tick + 1; }
void chat_assets_profiles_retry(chat_assets *a)
{
    for (size_t i = 0; a && i < AVATAR_COUNT; ++i)
        if (*a->avatars[i].id && !a->avatars[i].path && !a->avatars[i].requested) a->avatars[i].dirty = true;
}

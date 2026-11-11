#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "net/gif_search.h"
#include "model/text/translation.h"
#include <glib.h>
#include <jansson.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define GIF_ENDPOINT "https://commons.wikimedia.org/w/api.php"
#define GIF_PAUSE_MS 400U
#define GIF_LIMIT 30U
#define GIF_MAX_BYTES (10U * 1024U * 1024U)
#define GIF_MAX_SIDE 4096

struct gif_search {
    media_download *download;
    gif_result *results;
    size_t count;
    gif_state state;
    bool pending;
    uint64_t due;
    char *query;
    const char *error;
};

static bool wikimedia_host(const char *url)
{
    if (!g_str_has_prefix(url, "https://"))
        return false;
    const char *host = url + 8;
    size_t length = strcspn(host, "/:?#");
    static const char suffix[] = ".wikimedia.org";
    size_t n = sizeof(suffix) - 1;
    return length > n && !g_ascii_strncasecmp(host + length - n, suffix, n) &&
           !memchr(host, '@', length);
}
static bool accepted(const char *url, const char *origin)
{
    if (!url || strlen(url) > 1024)
        return false;
    for (const unsigned char *p = (const unsigned char *)url; *p; ++p)
        if (*p <= 0x20 || *p == 0x7f)
            return false;
    if (origin) {
        size_t n = strlen(origin);
        return n && !strncmp(url, origin, n) && url[n] == '/';
    }
    return wikimedia_host(url);
}
/* Remove the namespace and extension, replace underscores and drop controls
 * and bidirectional overrides, which would let a title rearrange the screen. */
static char *clean_title(const char *raw)
{
    char *valid = g_utf8_make_valid(raw ? raw : "", -1);
    const char *name = g_str_has_prefix(valid, "File:") ? valid + 5 : valid;
    GString *out = g_string_new(NULL);
    for (const char *p = name; *p; p = g_utf8_next_char(p)) {
        gunichar c = g_utf8_get_char(p);
        if (c == '_' || g_unichar_iscntrl(c) || c == 0x2028 || c == 0x2029 ||
            (c >= 0x202a && c <= 0x202e) || (c >= 0x2066 && c <= 0x2069))
            c = ' ';
        if (c != ' ' || (out->len && out->str[out->len - 1] != ' '))
            g_string_append_unichar(out, c);
    }
    g_free(valid);
    if (out->len > 4 && !g_ascii_strcasecmp(out->str + out->len - 4, ".gif"))
        g_string_truncate(out, out->len - 4);
    if (out->len > 120) {
        char *end = g_utf8_find_prev_char(out->str, out->str + 120);
        g_string_truncate(out, end ? (gsize)(end - out->str) : 120);
    }
    g_strstrip(out->str);
    return g_string_free(out, FALSE);
}
void gif_results_free(gif_result *results, size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        g_free(results[i].title);
        g_free(results[i].thumb_url);
        g_free(results[i].url);
    }
    free(results);
}
static int compare_index(const void *a, const void *b)
{
    const json_t *x = *(json_t *const *)a, *y = *(json_t *const *)b;
    json_int_t i = json_integer_value(json_object_get(x, "index")),
               j = json_integer_value(json_object_get(y, "index"));
    return i < j ? -1 : i > j;
}
bool gif_search_parse(const char *text, size_t length, const char *origin, gif_result **results,
                      size_t *count)
{
    *results = NULL;
    *count = 0;
    json_error_t error;
    json_t *root = json_loadb(text, length, 0, &error);
    if (!json_is_object(root)) {
        json_decref(root);
        return false;
    }
    /* A query without results has no "pages" member at all. */
    json_t *pages = json_object_get(json_object_get(root, "query"), "pages");
    size_t total = json_is_array(pages) ? json_array_size(pages) : 0;
    if (total > GIF_LIMIT * 2)
        total = GIF_LIMIT * 2;
    json_t **ordered = calloc(total ? total : 1, sizeof(*ordered));
    gif_result *out = calloc(total ? total : 1, sizeof(*out));
    if (!ordered || !out) {
        free(ordered);
        free(out);
        json_decref(root);
        return false;
    }
    for (size_t i = 0; i < total; ++i)
        ordered[i] = json_array_get(pages, i);
    qsort(ordered, total, sizeof(*ordered), compare_index);
    size_t n = 0;
    for (size_t i = 0; i < total && n < GIF_LIMIT; ++i) {
        json_t *info = json_array_get(json_object_get(ordered[i], "imageinfo"), 0);
        const char *url = json_string_value(json_object_get(info, "url"));
        const char *thumb = json_string_value(json_object_get(info, "thumburl"));
        const char *mime = json_string_value(json_object_get(info, "mime"));
        json_int_t size = json_integer_value(json_object_get(info, "size"));
        json_int_t width = json_integer_value(json_object_get(info, "width"));
        json_int_t height = json_integer_value(json_object_get(info, "height"));
        if (!mime || strcmp(mime, "image/gif") || size <= 0 || size > GIF_MAX_BYTES || width < 1 ||
            height < 1 || width > GIF_MAX_SIDE || height > GIF_MAX_SIDE || !accepted(url, origin) ||
            !accepted(thumb, origin))
            continue;
        out[n].title = clean_title(json_string_value(json_object_get(ordered[i], "title")));
        out[n].url = g_strdup(url);
        out[n].thumb_url = g_strdup(thumb);
        out[n].width = (int)width;
        out[n].height = (int)height;
        ++n;
    }
    free(ordered);
    json_decref(root);
    *results = out;
    *count = n;
    return true;
}

gif_search *gif_search_create(auth_client *auth)
{
    gif_search *s = calloc(1, sizeof(*s));
    if (!s)
        return NULL;
    s->download = media_download_create(auth);
    if (!s->download) {
        free(s);
        return NULL;
    }
    return s;
}
static void clear_results(gif_search *s)
{
    gif_results_free(s->results, s->count);
    s->results = NULL;
    s->count = 0;
}
void gif_search_destroy(gif_search *s)
{
    if (!s)
        return;
    media_download_destroy(s->download);
    clear_results(s);
    g_free(s->query);
    free(s);
}
void gif_search_request(gif_search *s, const char *query, uint64_t now)
{
    if (!s)
        return;
    media_download_cancel(s->download);
    g_free(s->query);
    s->query = g_strdup(query ? query : "");
    s->due = now + (s->state == GIF_SEARCH_IDLE ? 0 : GIF_PAUSE_MS);
    s->pending = true;
    s->state = GIF_SEARCH_LOADING;
    s->error = NULL;
}
static const char *endpoint(void)
{
    const char *custom = getenv("PAPO_GIF_SEARCH_URL");
    return custom && *custom ? custom : GIF_ENDPOINT;
}
/* The scheme, host and port of the test endpoint, or NULL for Wikimedia. */
static char *custom_origin(void)
{
    const char *custom = getenv("PAPO_GIF_SEARCH_URL");
    if (!custom || !*custom)
        return NULL;
    const char *rest = strstr(custom, "://");
    if (!rest)
        return g_strdup("");
    rest += 3;
    return g_strndup(custom, (gsize)(rest + strcspn(rest, "/?#") - custom));
}
static void start(gif_search *s)
{
    char *terms = g_strdup_printf("%s%sfilemime:image/gif", s->query, *s->query ? " " : "");
    char *escaped = g_uri_escape_string(terms, NULL, FALSE);
    char *url = g_strdup_printf("%s?action=query&format=json&formatversion=2&generator=search"
                                "&gsrnamespace=6&gsrlimit=%u&gsrsearch=%s&prop=imageinfo"
                                "&iiprop=url%%7Csize%%7Cmime&iiurlwidth=240",
                                endpoint(), GIF_LIMIT, escaped);
    s->pending = false;
    if (!media_download_json_request(s->download, url)) {
        s->state = GIF_SEARCH_FAILED;
        s->error = _("Não foi possível iniciar a busca de GIFs.");
    }
    g_free(url);
    g_free(escaped);
    g_free(terms);
}
static void finish(gif_search *s)
{
    char *path = media_download_take_file(s->download);
    char *contents = NULL;
    gsize length = 0;
    clear_results(s);
    char *origin = custom_origin();
    if (path && g_file_get_contents(path, &contents, &length, NULL) &&
        gif_search_parse(contents, length, origin, &s->results, &s->count)) {
        s->state = GIF_SEARCH_READY;
    } else {
        s->state = GIF_SEARCH_FAILED;
        s->error = _("Resposta inválida da busca de GIFs.");
    }
    g_free(origin);
    g_free(contents);
    if (path)
        unlink(path);
    free(path);
}
bool gif_search_poll(gif_search *s, uint64_t now)
{
    if (!s || s->state != GIF_SEARCH_LOADING)
        return false;
    if (s->pending) {
        if (now < s->due)
            return false;
        start(s);
        return s->state == GIF_SEARCH_FAILED;
    }
    media_download_poll(s->download);
    media_state state = media_download_state(s->download);
    if (state == MEDIA_READY) {
        finish(s);
        return true;
    }
    if (state == MEDIA_FAILED || state == MEDIA_IDLE) {
        s->state = GIF_SEARCH_FAILED;
        s->error = media_download_http_status(s->download) == 429
                       ? _("Limite de requisições. Aguarde e tente novamente.")
                       : _("Não foi possível buscar GIFs. Verifique a conexão.");
        return true;
    }
    return false;
}
gif_state gif_search_state(const gif_search *s)
{
    return s ? s->state : GIF_SEARCH_IDLE;
}
size_t gif_search_count(const gif_search *s)
{
    return s ? s->count : 0;
}
const gif_result *gif_search_result(const gif_search *s, size_t index)
{
    return s && index < s->count ? &s->results[index] : NULL;
}
const char *gif_search_error(const gif_search *s)
{
    return s && s->error ? s->error : "";
}

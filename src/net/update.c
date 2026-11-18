#include "net/update.h"
#include <glib.h>
#include <jansson.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define UPDATE_ENDPOINT "https://api.github.com/repos/prodesert22/papo-tui"
/* More than one page is never needed: the list is newest first. */
#define UPDATE_LIST_SIZE 5
#define UPDATE_LIST_LIMIT 100U
#define UPDATE_ASSET_COUNT 1000U

const char *const update_request_headers[] = {"Accept: application/vnd.github+json",
                                              "X-GitHub-Api-Version: 2022-11-28"};
const size_t update_request_header_count =
    sizeof(update_request_headers) / sizeof(update_request_headers[0]);

bool update_request_url(bool prereleases, char *url, size_t size)
{
    const char *custom = getenv("PAPO_UPDATE_API_URL");
    const char *endpoint = custom && *custom ? custom : UPDATE_ENDPOINT;
    int written = prereleases
                      ? snprintf(url, size, "%s/releases?per_page=%d", endpoint, UPDATE_LIST_SIZE)
                      : snprintf(url, size, "%s/releases/latest", endpoint);
    return written > 0 && (size_t)written < size;
}
/* -1 malformed, 0 not eligible, 1 version read. */
static int release_version(const json_t *release, bool prereleases, app_version *version)
{
    const json_t *tag = json_object_get(release, "tag_name");
    const json_t *draft = json_object_get(release, "draft");
    const json_t *prerelease = json_object_get(release, "prerelease");
    const char *name = json_string_value(tag);
    if (!json_is_object(release) || !name || !json_is_boolean(draft) ||
        !json_is_boolean(prerelease) || strlen(name) != json_string_length(tag))
        return -1;
    if (json_is_true(draft) || name[0] != 'v' || !app_version_parse(name + 1, version))
        return 0;
    /* A final tag still marked as a pre-release is not offered to final builds. */
    return prereleases || (version->stage == VERSION_FINAL && !json_is_true(prerelease));
}
static bool hash_text(const char *text)
{
    size_t length = strlen(text);
    if (length != UPDATE_HASH_BYTES - 1)
        return false;
    for (size_t i = 0; i < length; ++i)
        if (!((text[i] >= '0' && text[i] <= '9') || (text[i] >= 'a' && text[i] <= 'f')))
            return false;
    return true;
}
bool update_asset_valid(const update_asset *asset)
{
    /* Both texts must end inside their arrays before they are measured. */
    if (!asset || !memchr(asset->name, 0, sizeof(asset->name)) ||
        !memchr(asset->sha256, 0, sizeof(asset->sha256)))
        return false;
    size_t length = strlen(asset->name);
    if (!length || strchr(asset->name, '/') || strchr(asset->name, '\\') || asset->name[0] == '.')
        return false;
    for (size_t i = 0; i < length; ++i)
        if ((unsigned char)asset->name[i] <= ' ' || (unsigned char)asset->name[i] >= 0x7f)
            return false;
    return hash_text(asset->sha256) && asset->size > 0 && asset->size <= UPDATE_ASSET_LIMIT;
}
/* The file of this release for the installation, when it is listed with a
 * SHA-256 digest and a size the program accepts. */
static void release_asset(const json_t *release, const app_version *version,
                          install_channel channel, const char *architecture, update_asset *asset)
{
    char text[APP_VERSION_TEXT], name[INSTALL_ASSET_BYTES];
    memset(asset, 0, sizeof(*asset));
    const json_t *assets = json_object_get(release, "assets");
    if (!json_is_array(assets) || json_array_size(assets) > UPDATE_ASSET_COUNT ||
        !app_version_format(version, text, sizeof(text)) ||
        !install_asset_name(channel, architecture, text, name, sizeof(name)))
        return;
    size_t index;
    json_t *listed;
    json_array_foreach(assets, index, listed)
    {
        const json_t *title = json_object_get(listed, "name");
        const json_t *digest = json_object_get(listed, "digest");
        const json_t *size = json_object_get(listed, "size");
        const char *found = json_string_value(title), *sum = json_string_value(digest);
        if (!found || strlen(found) != json_string_length(title) || strcmp(found, name))
            continue;
        update_asset candidate = {0};
        snprintf(candidate.name, sizeof(candidate.name), "%s", name);
        if (sum && strlen(sum) == json_string_length(digest) && !strncmp(sum, "sha256:", 7) &&
            strlen(sum + 7) == UPDATE_HASH_BYTES - 1)
            memcpy(candidate.sha256, sum + 7, UPDATE_HASH_BYTES);
        if (json_is_integer(size) && json_integer_value(size) > 0)
            candidate.size = (uint64_t)json_integer_value(size);
        /* A file that cannot be checked is not offered. */
        if (update_asset_valid(&candidate))
            *asset = candidate;
        return;
    }
}
update_parse update_parse_releases(const char *json, size_t length, bool prereleases,
                                   install_channel channel, const char *architecture,
                                   app_version *latest, update_asset *asset)
{
    json_t *root = json ? json_loadb(json, length, JSON_REJECT_DUPLICATES, NULL) : NULL;
    update_parse result = UPDATE_PARSE_NONE;
    const json_t *chosen = NULL;
    app_version version;
    memset(asset, 0, sizeof(*asset));
    if (json_is_object(root)) {
        int read = release_version(root, prereleases, &version);
        if (read < 0)
            result = UPDATE_PARSE_INVALID;
        else if (read) {
            *latest = version;
            chosen = root;
            result = UPDATE_PARSE_FOUND;
        }
    } else if (json_is_array(root) && json_array_size(root) <= UPDATE_LIST_LIMIT) {
        size_t index;
        json_t *release;
        json_array_foreach(root, index, release)
        {
            int read = release_version(release, prereleases, &version);
            if (read < 0) {
                result = UPDATE_PARSE_INVALID;
                break;
            }
            if (read &&
                (result == UPDATE_PARSE_NONE || app_version_compare(&version, latest) > 0)) {
                *latest = version;
                chosen = release;
                result = UPDATE_PARSE_FOUND;
            }
        }
    } else
        result = UPDATE_PARSE_INVALID;
    if (result == UPDATE_PARSE_FOUND)
        release_asset(chosen, latest, channel, architecture, asset);
    json_decref(root);
    return result;
}
/* The scheme, host and port of the test endpoint; NULL without one. */
static char *custom_origin(void)
{
    const char *custom = getenv("PAPO_UPDATE_API_URL");
    if (!custom || !*custom)
        return NULL;
    const char *rest = strstr(custom, "://");
    if (!rest)
        return g_strdup("");
    rest += 3;
    return g_strndup(custom, (gsize)(rest + strcspn(rest, "/?#") - custom));
}
bool update_release_url(const app_version *version, const char *file, char *url, size_t size)
{
    char text[APP_VERSION_TEXT];
    const char *custom = getenv("PAPO_UPDATE_API_URL");
    if (!file || !*file || !app_version_format(version, text, sizeof(text)))
        return false;
    int written = custom && *custom ? snprintf(url, size, "%s/download/v%s/%s", custom, text, file)
                                    : snprintf(url, size, "%s/releases/download/v%s/%s",
                                               UPDATE_REPOSITORY, text, file);
    return written > 0 && (size_t)written < size;
}
bool update_asset_url(const app_version *version, const update_asset *asset, char *url, size_t size)
{
    return update_asset_valid(asset) && update_release_url(version, asset->name, url, size);
}
bool update_manifest_hash(const char *manifest, size_t length, const char *name,
                          char sha256[UPDATE_HASH_BYTES])
{
    bool found = false;
    size_t wanted = name ? strlen(name) : 0;
    if (!manifest || !wanted || memchr(manifest, 0, length))
        return false;
    for (const char *line = manifest, *end = manifest + length; line < end;) {
        const char *stop = memchr(line, '\n', (size_t)(end - line));
        size_t size = (size_t)((stop ? stop : end) - line);
        if (size && line[size - 1] == '\r')
            --size;
        /* "<64 hex digits>  <name>", as sha256sum writes it; "*" marks binary mode. */
        if (size == UPDATE_HASH_BYTES - 1 + 2 + wanted && line[UPDATE_HASH_BYTES - 1] == ' ' &&
            (line[UPDATE_HASH_BYTES] == ' ' || line[UPDATE_HASH_BYTES] == '*') &&
            !memcmp(line + UPDATE_HASH_BYTES + 1, name, wanted)) {
            char sum[UPDATE_HASH_BYTES];
            memcpy(sum, line, UPDATE_HASH_BYTES - 1);
            sum[UPDATE_HASH_BYTES - 1] = 0;
            /* A name listed twice with different sums names no file. */
            if (!hash_text(sum) || (found && strcmp(sum, sha256)))
                return false;
            memcpy(sha256, sum, UPDATE_HASH_BYTES);
            found = true;
        }
        line = stop ? stop + 1 : end;
    }
    return found;
}
bool update_redirect_allowed(const char *url, void *context)
{
    (void)context;
    if (!url || strncmp(url, "https://", 8))
        return false;
    char *origin = custom_origin();
    if (origin) {
        size_t length = strlen(origin);
        bool same = length && !strncmp(url, origin, length) && (url[length] == '/' || !url[length]);
        g_free(origin);
        return same;
    }
    GUri *parsed = g_uri_parse(url, G_URI_FLAGS_NONE, NULL);
    const char *host = parsed ? g_uri_get_host(parsed) : NULL;
    bool allowed =
        host && !g_uri_get_userinfo(parsed) &&
        (g_uri_get_port(parsed) == -1 || g_uri_get_port(parsed) == 443) &&
        (!strcmp(host, "github.com") || g_str_has_suffix(host, ".githubusercontent.com"));
    if (parsed)
        g_uri_unref(parsed);
    return allowed;
}

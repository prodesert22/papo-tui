#include "net/update.h"
#include <jansson.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define UPDATE_ENDPOINT "https://api.github.com/repos/prodesert22/papo-tui"
/* More than one page is never needed: the list is newest first. */
#define UPDATE_LIST_SIZE 5
#define UPDATE_LIST_LIMIT 100U

const char *const update_request_headers[] = {"Accept: application/vnd.github+json",
                                              "X-GitHub-Api-Version: 2022-11-28"};
const size_t update_request_header_count =
    sizeof(update_request_headers) / sizeof(update_request_headers[0]);

bool update_request_url(const app_version *current, char *url, size_t size, bool *list)
{
    const char *custom = getenv("PAPO_UPDATE_API_URL");
    const char *endpoint = custom && *custom ? custom : UPDATE_ENDPOINT;
    *list = current->stage != VERSION_FINAL;
    int written = *list ? snprintf(url, size, "%s/releases?per_page=%d", endpoint, UPDATE_LIST_SIZE)
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
update_parse update_parse_releases(const char *json, size_t length, bool prereleases,
                                   app_version *latest)
{
    json_t *root = json ? json_loadb(json, length, JSON_REJECT_DUPLICATES, NULL) : NULL;
    update_parse result = UPDATE_PARSE_NONE;
    app_version version;
    if (json_is_object(root)) {
        int read = release_version(root, prereleases, &version);
        if (read < 0)
            result = UPDATE_PARSE_INVALID;
        else if (read) {
            *latest = version;
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
                result = UPDATE_PARSE_FOUND;
            }
        }
    } else
        result = UPDATE_PARSE_INVALID;
    json_decref(root);
    return result;
}

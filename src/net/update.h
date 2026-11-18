#ifndef PAPO_NET_UPDATE_H
#define PAPO_NET_UPDATE_H

#include "model/version.h"
#include <stdbool.h>
#include <stddef.h>

/* The releases of this program on GitHub, read through its public REST API. */
#define UPDATE_REPOSITORY "https://github.com/prodesert22/papo-tui"
#define UPDATE_RESPONSE_LIMIT (2U * 1024U * 1024U)
#define UPDATE_URL_BYTES 2049U

typedef enum {
    UPDATE_PARSE_FOUND,  /* latest holds the newest eligible release. */
    UPDATE_PARSE_NONE,   /* A valid answer without an eligible release. */
    UPDATE_PARSE_INVALID /* Not the documented shape. */
} update_parse;

/* The request that answers "what is the newest release a build of this version
 * may move to": a final version asks for the latest final release, a
 * pre-release lists the newest releases of any kind. PAPO_UPDATE_API_URL
 * replaces the repository endpoint, for tests. list reports which form it is. */
bool update_request_url(const app_version *current, char *url, size_t size, bool *list);
/* The request headers GitHub documents for this API. */
extern const char *const update_request_headers[];
extern const size_t update_request_header_count;
/* Reads one release object or a list of them. Drafts never count; releases
 * with a pre-release tag or marked as pre-release count only when asked; a tag
 * that is not a release version of this program is skipped. Nothing else from
 * the answer is used or kept. */
update_parse update_parse_releases(const char *json, size_t length, bool prereleases,
                                   app_version *latest);

#endif

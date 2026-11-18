#ifndef PAPO_NET_UPDATE_H
#define PAPO_NET_UPDATE_H

#include "model/version.h"
#include "platform/install.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The releases of this program on GitHub, read through its public REST API. */
#define UPDATE_REPOSITORY "https://github.com/prodesert22/papo-tui"
#define UPDATE_RESPONSE_LIMIT (2U * 1024U * 1024U)
#define UPDATE_URL_BYTES 2049U
/* The largest package ever published is about 62 MB. */
#define UPDATE_ASSET_LIMIT (256U * 1024U * 1024U)
#define UPDATE_HASH_BYTES 65U

typedef enum {
    UPDATE_PARSE_FOUND,  /* latest holds the newest eligible release. */
    UPDATE_PARSE_NONE,   /* A valid answer without an eligible release. */
    UPDATE_PARSE_INVALID /* Not the documented shape. */
} update_parse;
/* The file of a release that replaces one kind of installation. Its name is
 * worked out here, never taken from the answer; the answer only says whether
 * the release has that file, how large it is and its SHA-256. */
typedef struct {
    char name[INSTALL_ASSET_BYTES]; /* Empty when the release has no such file. */
    char sha256[UPDATE_HASH_BYTES]; /* Lowercase hexadecimal. */
    uint64_t size;
} update_asset;

/* The request that answers "what is the newest release this copy may move
 * to": the latest final release, or, when pre-releases are accepted, the list
 * of the newest releases of any kind. PAPO_UPDATE_API_URL replaces the
 * repository endpoint, for tests. */
bool update_request_url(bool prereleases, char *url, size_t size);
/* The request headers GitHub documents for this API. */
extern const char *const update_request_headers[];
extern const size_t update_request_header_count;
/* Reads one release object or a list of them. Drafts never count; releases
 * with a pre-release tag or marked as pre-release count only when asked; a tag
 * that is not a release version of this program is skipped. asset receives the
 * file of the chosen release for an installation of that channel and
 * architecture, when the release lists it with a usable size and digest.
 * Nothing else from the answer is used or kept. */
update_parse update_parse_releases(const char *json, size_t length, bool prereleases,
                                   install_channel channel, const char *architecture,
                                   app_version *latest, update_asset *asset);
/* A name, a 64-digit lowercase hash and a size within the limit. */
bool update_asset_valid(const update_asset *asset);
/* Where a release keeps a file: built from the version and the name, under the
 * repository's download address (or the test endpoint's). */
bool update_asset_url(const app_version *version, const update_asset *asset, char *url,
                      size_t size);
/* Where a release keeps any of its files, such as the manifest of checksums and
 * its signature. */
#define UPDATE_MANIFEST "SHA256SUMS"
#define UPDATE_MANIFEST_SIGNATURE "SHA256SUMS.asc"
#define UPDATE_MANIFEST_LIMIT (1024U * 1024U)
bool update_release_url(const app_version *version, const char *file, char *url, size_t size);
/* The SHA-256 a manifest in sha256sum's format lists for a file name; false
 * when it lists none, or lists the name with two different sums. The manifest
 * is borrowed text of that length. */
bool update_manifest_hash(const char *manifest, size_t length, const char *name,
                          char sha256[UPDATE_HASH_BYTES]);
/* Whether a download may follow a redirect there: https on GitHub's own hosts,
 * or the origin of the test endpoint. context is unused. */
bool update_redirect_allowed(const char *url, void *context);

#endif

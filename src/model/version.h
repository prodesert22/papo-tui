#ifndef PAPO_VERSION_MODEL_H
#define PAPO_VERSION_MODEL_H

#include <stdbool.h>
#include <stddef.h>

typedef enum { VERSION_ALPHA, VERSION_BETA, VERSION_RC, VERSION_FINAL } version_stage;

/* A release version: MAJOR.MINOR.PATCH, or MAJOR.MINOR.PATCH-(alpha|beta|rc).N
 * with N from 1, the same grammar the release tags follow. */
typedef struct {
    unsigned major, minor, patch;
    version_stage stage;
    unsigned number; /* Pre-release number; zero for a final version. */
} app_version;

#define APP_VERSION_TEXT 48U

/* Strict parse of borrowed text: no prefix, no leading zeros, nothing after
 * the version, numbers of at most six digits. */
bool app_version_parse(const char *text, app_version *version);
/* The version this program was built as. Development builds end in "-dev",
 * which is reported apart and not part of the version. */
bool app_version_parse_build(const char *text, app_version *version, bool *development);
/* SemVer precedence: 1.0.0-alpha.1 < 1.0.0-beta.1 < 1.0.0-rc.1 < 1.0.0. */
int app_version_compare(const app_version *left, const app_version *right);
/* Writes the canonical text; false when it does not fit. */
bool app_version_format(const app_version *version, char *text, size_t size);

#endif

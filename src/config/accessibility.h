#ifndef PAPO_ACCESSIBILITY_H
#define PAPO_ACCESSIBILITY_H

#include <jansson.h>
#include <stdbool.h>

/* Accessibility choices of this device. The server keeps no such settings, so
 * they live in config.json and apply to every server. */
typedef enum {
    ACCESS_CONTRAST_DEFAULT,
    ACCESS_CONTRAST_HIGH,
    /* The terminal's own colors only; focus and state use reverse and bold. */
    ACCESS_CONTRAST_NONE
} access_contrast;
typedef enum { ACCESS_ROLES_ORIGINAL, ACCESS_ROLES_ADJUSTED, ACCESS_ROLES_OFF } access_roles;
typedef struct {
    access_contrast contrast;
    /* Error, success, speaking and unread colors that stay apart with red-green
     * color vision deficiencies. */
    bool colorblind;
    /* Bold and underline on the focused item and border, so focus never
     * depends on color alone. */
    bool cues;
    access_roles roles;
} accessibility_config;

void accessibility_default(accessibility_config *config);
bool accessibility_valid(const accessibility_config *config);
/* config.json "accessibility": {"contrast": "default|high|none", "colorblind":
 * bool, "cues": bool, "role_colors": "original|adjusted|off"}. */
bool accessibility_decode(accessibility_config *config, const json_t *value);
json_t *accessibility_encode(const accessibility_config *config);
const char *accessibility_contrast_name(access_contrast contrast);
const char *accessibility_roles_name(access_roles roles);

/* The choices in effect for this process; NO_COLOR, when set and not empty,
 * turns colors off for this run without changing the saved choice. */
void accessibility_set(const accessibility_config *config);
accessibility_config accessibility_get(void);
access_contrast accessibility_contrast(void);

#endif

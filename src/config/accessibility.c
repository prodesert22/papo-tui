#include "config/accessibility.h"

#include <glib.h>
#include <stdlib.h>
#include <string.h>

static const char *const contrasts[] = {"default", "high", "none"};
static const char *const roles[] = {"original", "adjusted", "off"};

void accessibility_default(accessibility_config *config)
{
    memset(config, 0, sizeof(*config));
    config->contrast = ACCESS_CONTRAST_DEFAULT;
    config->roles = ACCESS_ROLES_ORIGINAL;
}

bool accessibility_valid(const accessibility_config *config)
{
    return config && config->contrast >= ACCESS_CONTRAST_DEFAULT &&
           config->contrast <= ACCESS_CONTRAST_NONE && config->roles >= ACCESS_ROLES_ORIGINAL &&
           config->roles <= ACCESS_ROLES_OFF;
}

const char *accessibility_contrast_name(access_contrast contrast)
{
    return contrast >= ACCESS_CONTRAST_DEFAULT && contrast <= ACCESS_CONTRAST_NONE
               ? contrasts[contrast]
               : contrasts[0];
}

const char *accessibility_roles_name(access_roles value)
{
    return value >= ACCESS_ROLES_ORIGINAL && value <= ACCESS_ROLES_OFF ? roles[value] : roles[0];
}

static bool choice(const json_t *value, const char *const *names, int count, int *result)
{
    const char *text = json_string_value(value);
    for (int i = 0; text && i < count; ++i)
        if (!strcmp(text, names[i])) {
            *result = i;
            return true;
        }
    return false;
}

bool accessibility_decode(accessibility_config *config, const json_t *value)
{
    accessibility_config next;
    accessibility_default(&next);
    int contrast, role;
    json_t *colorblind = json_object_get(value, "colorblind"),
           *cues = json_object_get(value, "cues");
    if (!json_is_object(value) || json_object_size(value) != 4 ||
        !choice(json_object_get(value, "contrast"), contrasts, 3, &contrast) ||
        !choice(json_object_get(value, "role_colors"), roles, 3, &role) ||
        !json_is_boolean(colorblind) || !json_is_boolean(cues))
        return false;
    next.contrast = (access_contrast)contrast;
    next.roles = (access_roles)role;
    next.colorblind = json_is_true(colorblind);
    next.cues = json_is_true(cues);
    *config = next;
    return true;
}

json_t *accessibility_encode(const accessibility_config *config)
{
    if (!accessibility_valid(config))
        return NULL;
    return json_pack("{s:s,s:b,s:b,s:s}", "contrast", accessibility_contrast_name(config->contrast),
                     "colorblind", config->colorblind, "cues", config->cues, "role_colors",
                     accessibility_roles_name(config->roles));
}

static GMutex lock;
static accessibility_config current;

void accessibility_set(const accessibility_config *config)
{
    if (!accessibility_valid(config))
        return;
    g_mutex_lock(&lock);
    current = *config;
    g_mutex_unlock(&lock);
}

accessibility_config accessibility_get(void)
{
    g_mutex_lock(&lock);
    accessibility_config config = current;
    g_mutex_unlock(&lock);
    const char *no_color = getenv("NO_COLOR");
    if (no_color && *no_color)
        config.contrast = ACCESS_CONTRAST_NONE;
    return config;
}

access_contrast accessibility_contrast(void)
{
    return accessibility_get().contrast;
}

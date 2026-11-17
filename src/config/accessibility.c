#include "config/accessibility.h"

#include <glib.h>
#include <stdlib.h>
#include <string.h>

static const char *const contrasts[] = {"default", "high", "none"};
static const char *const roles[] = {"original", "adjusted", "off"};
static const char *const symbols[] = {"icons", "text"};
static const char *const emoji[] = {"images", "names"};
static const char *const borders[] = {"lines", "ascii"};
static const char *const alerts[] = {"off", "highlight", "flash"};
static const char *const durations[] = {"short", "long", "keypress"};

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
           config->roles <= ACCESS_ROLES_OFF && config->visual_alert >= ACCESS_ALERT_OFF &&
           config->visual_alert <= ACCESS_ALERT_FLASH && config->notices >= ACCESS_NOTICE_SHORT &&
           config->notices <= ACCESS_NOTICE_KEYPRESS;
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

bool accessibility_decode(accessibility_config *config, const json_t *value, int version)
{
    accessibility_config next;
    accessibility_default(&next);
    int contrast, role;
    json_t *colorblind = json_object_get(value, "colorblind"),
           *cues = json_object_get(value, "cues");
    if (!json_is_object(value) ||
        json_object_size(value) != (version >= 13   ? 12U
                                    : version >= 12 ? 8U
                                                    : 4U) ||
        !choice(json_object_get(value, "contrast"), contrasts, 3, &contrast) ||
        !choice(json_object_get(value, "role_colors"), roles, 3, &role) ||
        !json_is_boolean(colorblind) || !json_is_boolean(cues))
        return false;
    next.contrast = (access_contrast)contrast;
    next.roles = (access_roles)role;
    next.colorblind = json_is_true(colorblind);
    next.cues = json_is_true(cues);
    if (version >= 12) {
        json_t *reader = json_object_get(value, "screen_reader");
        int symbol, names, border;
        if (!json_is_boolean(reader) ||
            !choice(json_object_get(value, "symbols"), symbols, 2, &symbol) ||
            !choice(json_object_get(value, "message_emoji"), emoji, 2, &names) ||
            !choice(json_object_get(value, "borders"), borders, 2, &border))
            return false;
        next.screen_reader = json_is_true(reader);
        next.text_symbols = symbol == 1;
        next.emoji_names = names == 1;
        next.ascii_borders = border == 1;
    }
    if (version >= 13) {
        json_t *mouse = json_object_get(value, "mouse"),
               *confirm = json_object_get(value, "confirm_exit");
        int alert, duration;
        if (!json_is_boolean(mouse) || !json_is_boolean(confirm) ||
            !choice(json_object_get(value, "visual_alert"), alerts, 3, &alert) ||
            !choice(json_object_get(value, "notice_duration"), durations, 3, &duration))
            return false;
        next.visual_alert = (access_alert)alert;
        next.notices = (access_notice)duration;
        next.mouse_off = !json_is_true(mouse);
        next.confirm_exit = json_is_true(confirm);
    }
    *config = next;
    return true;
}

json_t *accessibility_encode(const accessibility_config *config)
{
    if (!accessibility_valid(config))
        return NULL;
    return json_pack(
        "{s:s,s:b,s:b,s:s,s:b,s:s,s:s,s:s,s:s,s:s,s:b,s:b}", "contrast",
        accessibility_contrast_name(config->contrast), "colorblind", config->colorblind, "cues",
        config->cues, "role_colors", accessibility_roles_name(config->roles), "screen_reader",
        config->screen_reader, "symbols", symbols[config->text_symbols ? 1 : 0], "message_emoji",
        emoji[config->emoji_names ? 1 : 0], "borders", borders[config->ascii_borders ? 1 : 0],
        "visual_alert", alerts[config->visual_alert], "notice_duration", durations[config->notices],
        "mouse", !config->mouse_off, "confirm_exit", config->confirm_exit);
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

long long accessibility_notice_us(void)
{
    access_notice notices = accessibility_get().notices;
    return notices == ACCESS_NOTICE_KEYPRESS ? 0
           : notices == ACCESS_NOTICE_LONG   ? 6000000LL
                                             : 2000000LL;
}

#include "model/user_config.h"

#include <string.h>

static const char *const themes[] = {"dark", "light", "system"};
static const char *const font_sizes[] = {"small", "medium", "huge"};
static const char *const densities[] = {"compact", "normal", "comfortable"};
#define COUNT(array) (sizeof(array) / sizeof((array)[0]))

void user_config_defaults(user_config *config)
{
    *config = (user_config){.theme = USER_THEME_SYSTEM,
                            .notifications = true,
                            .preview = true,
                            .sound = true,
                            .mentions = true,
                            .font_size = USER_FONT_MEDIUM,
                            .density = USER_DENSITY_NORMAL,
                            .timestamps = true,
                            .avatars = true};
}

/* Index of value in names, or -1. Strings with an embedded NUL never match. */
static int enum_index(const json_t *value, const char *const *names, size_t count)
{
    if (!json_is_string(value) || strlen(json_string_value(value)) != json_string_length(value))
        return -1;
    for (size_t i = 0; i < count; ++i)
        if (!strcmp(json_string_value(value), names[i]))
            return (int)i;
    return -1;
}
static void read_enum(int *out, const json_t *object, const char *key, const char *const *names,
                      size_t count)
{
    int index = enum_index(json_object_get(object, key), names, count);
    if (index >= 0)
        *out = index;
}
static void read_bool(bool *out, const json_t *object, const char *key)
{
    const json_t *value = json_object_get(object, key);
    if (json_is_boolean(value))
        *out = json_is_true(value);
}

void user_config_read(user_config *config, const json_t *object)
{
    user_config_defaults(config);
    if (!json_is_object(object))
        return;
    int theme = (int)config->theme, font = (int)config->font_size, density = (int)config->density;
    read_enum(&theme, object, "theme", themes, COUNT(themes));
    const json_t *notifications = json_object_get(object, "notifications");
    if (json_is_object(notifications)) {
        read_bool(&config->notifications, notifications, "enabled");
        read_bool(&config->preview, notifications, "messagePreview");
        read_bool(&config->sound, notifications, "sound");
        read_bool(&config->mentions, notifications, "mentions");
    }
    const json_t *display = json_object_get(object, "display");
    if (json_is_object(display)) {
        read_enum(&font, display, "fontSize", font_sizes, COUNT(font_sizes));
        read_enum(&density, display, "messageDensity", densities, COUNT(densities));
        read_bool(&config->timestamps, display, "showTimestamps");
        read_bool(&config->avatars, display, "showAvatars");
    }
    config->theme = (user_theme)theme;
    config->font_size = (user_font_size)font;
    config->density = (user_density)density;
}

static bool valid_enum(const json_t *object, const char *key, const char *const *names,
                       size_t count)
{
    const json_t *value = json_object_get(object, key);
    return !value || enum_index(value, names, count) >= 0;
}
static bool valid_bools(const json_t *object, const char *const *keys, size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        const json_t *value = json_object_get(object, keys[i]);
        if (value && !json_is_boolean(value))
            return false;
    }
    return true;
}

bool user_config_valid(const json_t *object)
{
    static const char *const notification_keys[] = {"enabled", "messagePreview", "sound",
                                                    "mentions"};
    static const char *const display_keys[] = {"showTimestamps", "showAvatars"};
    if (!json_is_object(object) || !valid_enum(object, "theme", themes, COUNT(themes)))
        return false;
    const json_t *notifications = json_object_get(object, "notifications");
    if (notifications && (!json_is_object(notifications) ||
                          !valid_bools(notifications, notification_keys, COUNT(notification_keys))))
        return false;
    const json_t *display = json_object_get(object, "display");
    return !display || (json_is_object(display) &&
                        valid_enum(display, "fontSize", font_sizes, COUNT(font_sizes)) &&
                        valid_enum(display, "messageDensity", densities, COUNT(densities)) &&
                        valid_bools(display, display_keys, COUNT(display_keys)));
}

/* The base's object under key, copied, or a new one when it has none. */
static json_t *section(const json_t *base, const char *key)
{
    const json_t *value = json_object_get(base, key);
    return json_is_object(value) ? json_deep_copy(value) : json_object();
}

json_t *user_config_encode(const user_config *config, const json_t *base)
{
    json_t *out = json_is_object(base) ? json_deep_copy(base) : json_object();
    json_t *notifications = section(base, "notifications"), *display = section(base, "display");
    bool ok =
        out && notifications && display &&
        !json_object_set_new(out, "theme", json_string(themes[config->theme])) &&
        !json_object_set_new(notifications, "enabled", json_boolean(config->notifications)) &&
        !json_object_set_new(notifications, "messagePreview", json_boolean(config->preview)) &&
        !json_object_set_new(notifications, "sound", json_boolean(config->sound)) &&
        !json_object_set_new(notifications, "mentions", json_boolean(config->mentions)) &&
        !json_object_set_new(display, "fontSize", json_string(font_sizes[config->font_size])) &&
        !json_object_set_new(display, "messageDensity", json_string(densities[config->density])) &&
        !json_object_set_new(display, "showTimestamps", json_boolean(config->timestamps)) &&
        !json_object_set_new(display, "showAvatars", json_boolean(config->avatars));
    if (ok) {
        ok = !json_object_set(out, "notifications", notifications) &&
             !json_object_set(out, "display", display);
    }
    json_decref(notifications);
    json_decref(display);
    if (!ok) {
        json_decref(out);
        return NULL;
    }
    return out;
}

bool user_config_equal(const user_config *a, const user_config *b)
{
    return a->theme == b->theme && a->notifications == b->notifications &&
           a->preview == b->preview && a->sound == b->sound && a->mentions == b->mentions &&
           a->font_size == b->font_size && a->density == b->density &&
           a->timestamps == b->timestamps && a->avatars == b->avatars;
}

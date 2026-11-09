#include "model/user_config.h"
#undef NDEBUG
#include <assert.h>
#include <string.h>

static json_t *parse(const char *text)
{
    json_error_t error;
    json_t *value = json_loads(text, 0, &error);
    assert(value);
    return value;
}

static void partial_configs_keep_defaults(void)
{
    user_config c, defaults;
    user_config_defaults(&defaults);
    assert(defaults.theme == USER_THEME_SYSTEM && defaults.notifications && defaults.preview &&
           defaults.sound && defaults.mentions && defaults.font_size == USER_FONT_MEDIUM &&
           defaults.density == USER_DENSITY_NORMAL && defaults.timestamps && defaults.avatars);
    user_config_read(&c, NULL);
    assert(user_config_equal(&c, &defaults));
    json_t *partial = parse("{\"theme\":\"dark\"}");
    user_config_read(&c, partial);
    assert(c.theme == USER_THEME_DARK && c.sound && c.density == USER_DENSITY_NORMAL);
    json_decref(partial);
    /* Mistyped or unknown values never replace a default. */
    json_t *odd =
        parse("{\"theme\":\"blue\",\"notifications\":{\"sound\":\"no\",\"mentions\":false},"
              "\"display\":{\"messageDensity\":\"compact\",\"showAvatars\":0}}");
    user_config_read(&c, odd);
    assert(c.theme == USER_THEME_SYSTEM && c.sound && !c.mentions &&
           c.density == USER_DENSITY_COMPACT && c.avatars);
    assert(!user_config_valid(odd));
    json_decref(odd);
}

static void replies_are_checked_strictly(void)
{
    const char *valid[] = {
        "{}",
        "{\"theme\":\"light\",\"notifications\":{\"enabled\":false},\"display\":{}}",
        ("{\"display\":{\"fontSize\":\"huge\",\"messageDensity\":\"comfortable\","
         "\"showTimestamps\":false,\"showAvatars\":true},\"extra\":1}"),
    };
    const char *invalid[] = {
        "[]",
        "{\"theme\":\"\"}",
        "{\"theme\":null}",
        "{\"notifications\":[]}",
        "{\"notifications\":{\"enabled\":null}}",
        "{\"display\":{\"fontSize\":\"large\"}}",
        "{\"display\":{\"showTimestamps\":\"true\"}}",
    };
    for (size_t i = 0; i < sizeof(valid) / sizeof(*valid); ++i) {
        json_t *value = parse(valid[i]);
        assert(user_config_valid(value));
        json_decref(value);
    }
    for (size_t i = 0; i < sizeof(invalid) / sizeof(*invalid); ++i) {
        json_t *value = parse(invalid[i]);
        assert(!user_config_valid(value));
        json_decref(value);
    }
    assert(!user_config_valid(NULL));
}

/* The server replaces the whole config: every field is always sent, and
 * keys this client does not know survive the round trip. */
static void encoding_is_complete_and_keeps_unknown_keys(void)
{
    user_config c;
    user_config_defaults(&c);
    c.theme = USER_THEME_LIGHT;
    c.sound = false;
    c.density = USER_DENSITY_COMFORTABLE;
    c.font_size = USER_FONT_HUGE;
    json_t *base = parse("{\"future\":{\"x\":1},\"notifications\":{\"enabled\":false,\"later\":2},"
                         "\"display\":\"broken\"}");
    json_t *out = user_config_encode(&c, base);
    assert(out && user_config_valid(out));
    assert(json_integer_value(json_object_get(json_object_get(out, "future"), "x")) == 1);
    json_t *n = json_object_get(out, "notifications"), *d = json_object_get(out, "display");
    assert(json_integer_value(json_object_get(n, "later")) == 2);
    assert(json_is_true(json_object_get(n, "enabled")) &&
           json_is_true(json_object_get(n, "messagePreview")) &&
           json_is_false(json_object_get(n, "sound")) &&
           json_is_true(json_object_get(n, "mentions")));
    assert(!strcmp(json_string_value(json_object_get(out, "theme")), "light"));
    assert(!strcmp(json_string_value(json_object_get(d, "fontSize")), "huge"));
    assert(!strcmp(json_string_value(json_object_get(d, "messageDensity")), "comfortable"));
    assert(json_is_true(json_object_get(d, "showTimestamps")) &&
           json_is_true(json_object_get(d, "showAvatars")));
    /* The base is copied, never changed. */
    assert(json_is_false(json_object_get(json_object_get(base, "notifications"), "enabled")));
    user_config back;
    user_config_read(&back, out);
    assert(user_config_equal(&back, &c));
    json_decref(out);
    json_decref(base);
    out = user_config_encode(&c, NULL);
    assert(out && json_object_size(out) == 3);
    json_decref(out);
}

int main(void)
{
    partial_configs_keep_defaults();
    replies_are_checked_strictly();
    encoding_is_complete_and_keeps_unknown_keys();
    return 0;
}

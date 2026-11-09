#ifndef PAPO_USER_CONFIG_H
#define PAPO_USER_CONFIG_H
#include <jansson.h>
#include <stdbool.h>
/* UserConfig of openapi.yml: per-account settings the server only stores. Their
 * meaning on this client: theme and display apply to the active server tab,
 * notifications to alerts of their own server. */
typedef enum { USER_THEME_DARK, USER_THEME_LIGHT, USER_THEME_SYSTEM } user_theme;
typedef enum { USER_FONT_SMALL, USER_FONT_MEDIUM, USER_FONT_HUGE } user_font_size;
typedef enum { USER_DENSITY_COMPACT, USER_DENSITY_NORMAL, USER_DENSITY_COMFORTABLE } user_density;
typedef struct {
    user_theme theme;
    bool notifications, preview, sound, mentions;
    /* The terminal owns the font: kept only so a save sends it back. */
    user_font_size font_size;
    user_density density;
    bool timestamps, avatars;
} user_config;

/* The contract defaults: system theme, every switch on, medium, normal. */
void user_config_defaults(user_config *config);
/* Reads a UserConfig leniently: absent or mistyped fields keep their default,
 * since stored configs may be partial. NULL or a non-object gives defaults. */
void user_config_read(user_config *config, const json_t *object);
/* Strict check of a server reply: an object whose present fields have the
 * documented types and enum values. */
bool user_config_valid(const json_t *object);
/* A new, complete UserConfig: base's unknown keys are kept and every field
 * known here is written, because the server replaces the whole config and
 * stores omitted switches as false. NULL on allocation failure. */
json_t *user_config_encode(const user_config *config, const json_t *base);
bool user_config_equal(const user_config *a, const user_config *b);
#endif

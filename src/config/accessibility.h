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
typedef enum { ACCESS_ALERT_OFF, ACCESS_ALERT_HIGHLIGHT, ACCESS_ALERT_FLASH } access_alert;
typedef enum { ACCESS_NOTICE_SHORT, ACCESS_NOTICE_LONG, ACCESS_NOTICE_KEYPRESS } access_notice;
typedef struct {
    access_contrast contrast;
    /* Error, success, speaking and unread colors that stay apart with red-green
     * color vision deficiencies. */
    bool colorblind;
    /* Bold and underline on the focused item and border, so focus never
     * depends on color alone. */
    bool cues;
    access_roles roles;
    /* The terminal cursor follows the focused item, spinners stand still and
     * interface symbols are shown as text. */
    bool screen_reader;
    /* Interface icons and symbols as text, as on the Linux console. */
    bool text_symbols;
    /* Emoji in messages as their names between colons, custom ones included. */
    bool emoji_names;
    /* Borders and lines drawn with + - | instead of line-drawing characters. */
    bool ascii_borders;
    /* What a new notification shows besides its sound: nothing, a notice in
     * the footer, or the notice and one flash of the screen. */
    access_alert visual_alert;
    /* How long notices that go away by themselves stay: 2 s, 6 s, or until
     * the next key. */
    access_notice notices;
    /* The application ignores the mouse, leaving it to the terminal. */
    bool mouse_off;
    /* Quitting, signing out and closing a DM ask for the same key again. */
    bool confirm_exit;
} accessibility_config;

void accessibility_default(accessibility_config *config);
bool accessibility_valid(const accessibility_config *config);
/* config.json "accessibility": {"contrast": "default|high|none", "colorblind":
 * bool, "cues": bool, "role_colors": "original|adjusted|off"}; from version 12
 * also "screen_reader": bool, "symbols": "icons|text", "message_emoji":
 * "images|names" and "borders": "lines|ascii". */
bool accessibility_decode(accessibility_config *config, const json_t *value, int version);
/* From version 13 also "visual_alert": "off|highlight|flash",
 * "notice_duration": "short|long|keypress", "mouse": bool and
 * "confirm_exit": bool. */
json_t *accessibility_encode(const accessibility_config *config);
const char *accessibility_contrast_name(access_contrast contrast);
const char *accessibility_roles_name(access_roles roles);

/* The choices in effect for this process; NO_COLOR, when set and not empty,
 * turns colors off for this run without changing the saved choice. */
void accessibility_set(const accessibility_config *config);
accessibility_config accessibility_get(void);
access_contrast accessibility_contrast(void);
/* Microseconds a notice that goes away by itself stays; 0 when it stays
 * until the next key. */
long long accessibility_notice_us(void);

#endif

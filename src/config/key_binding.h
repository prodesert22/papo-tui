#ifndef PAPO_KEY_BINDING_H
#define PAPO_KEY_BINDING_H

#include <stdbool.h>
#include <stddef.h>

/* A platform-independent key binding. `code` is a Unicode scalar for
 * KEY_BINDING_CHAR, a key_binding_code for KEY_BINDING_KEY and a mouse button
 * number for KEY_BINDING_MOUSE. */
typedef enum {
    KEY_BINDING_NONE = 0,
    KEY_BINDING_CHAR,
    KEY_BINDING_KEY,
    KEY_BINDING_MOUSE
} key_binding_kind;

enum { KEY_BINDING_SHIFT = 1, KEY_BINDING_ALT = 2, KEY_BINDING_CTRL = 4 };

typedef enum {
    KEY_BINDING_UP = 1,
    KEY_BINDING_DOWN,
    KEY_BINDING_LEFT,
    KEY_BINDING_RIGHT,
    KEY_BINDING_HOME,
    KEY_BINDING_END,
    KEY_BINDING_PAGE_UP,
    KEY_BINDING_PAGE_DOWN,
    KEY_BINDING_INSERT,
    KEY_BINDING_DELETE,
    KEY_BINDING_ENTER,
    KEY_BINDING_TAB,
    KEY_BINDING_BACKSPACE,
    KEY_BINDING_ESCAPE,
    KEY_BINDING_F1,
    KEY_BINDING_F2,
    KEY_BINDING_F3,
    KEY_BINDING_F4,
    KEY_BINDING_F5,
    KEY_BINDING_F6,
    KEY_BINDING_F7,
    KEY_BINDING_F8,
    KEY_BINDING_F9,
    KEY_BINDING_F10,
    KEY_BINDING_F11,
    KEY_BINDING_F12
} key_binding_code;

typedef struct {
    key_binding_kind kind;
    unsigned code;
    unsigned modifiers;
} key_binding;

/* Canonical strings use ctrl+alt+shift+name, e.g. "ctrl+shift+m", "mouse4" and
 * "ctrl+f12". A printable character is its UTF-8 encoding except space
 * ("space") and plus ("u002b"); parsing also accepts "uXXXX" code points. */
bool key_binding_parse(const char *text, key_binding *binding);
bool key_binding_format(const key_binding *binding, char *output, size_t size);
/* Friendly label for the settings panel. */
bool key_binding_label(const key_binding *binding, char *output, size_t size);
bool key_binding_valid(const key_binding *binding);
/* Human-readable reason for a rejected binding; NULL when it is valid. */
const char *key_binding_error(const key_binding *binding);
bool key_binding_equal(const key_binding *left, const key_binding *right);
/* Legacy terminals encode Ctrl+letter as one control byte. Returns that byte
 * when the binding can be recognized without an extended keyboard protocol,
 * or 0 when it cannot. */
unsigned key_binding_legacy_ctrl_byte(const key_binding *binding);
/* Old integer configuration values 2..12 meant Ctrl+F2..F12. */
bool key_binding_legacy(unsigned function_key, key_binding *binding);

#endif

#include "config/key_binding.h"
#include "model/text/translation.h"

#include <glib.h>
#include <stdio.h>
#include <string.h>

#define KEY_BINDING_TEXT 64

typedef struct {
    const char *name;
    key_binding_code code;
} binding_name;

static const binding_name key_names[] = {{"up", KEY_BINDING_UP},
                                         {"down", KEY_BINDING_DOWN},
                                         {"left", KEY_BINDING_LEFT},
                                         {"right", KEY_BINDING_RIGHT},
                                         {"home", KEY_BINDING_HOME},
                                         {"end", KEY_BINDING_END},
                                         {"pageup", KEY_BINDING_PAGE_UP},
                                         {"page-up", KEY_BINDING_PAGE_UP},
                                         {"pgup", KEY_BINDING_PAGE_UP},
                                         {"pagedown", KEY_BINDING_PAGE_DOWN},
                                         {"page-down", KEY_BINDING_PAGE_DOWN},
                                         {"pgdn", KEY_BINDING_PAGE_DOWN},
                                         {"insert", KEY_BINDING_INSERT},
                                         {"ins", KEY_BINDING_INSERT},
                                         {"delete", KEY_BINDING_DELETE},
                                         {"del", KEY_BINDING_DELETE},
                                         {"enter", KEY_BINDING_ENTER},
                                         {"return", KEY_BINDING_ENTER},
                                         {"tab", KEY_BINDING_TAB},
                                         {"backspace", KEY_BINDING_BACKSPACE},
                                         {"escape", KEY_BINDING_ESCAPE},
                                         {"esc", KEY_BINDING_ESCAPE},
                                         {"f1", KEY_BINDING_F1},
                                         {"f2", KEY_BINDING_F2},
                                         {"f3", KEY_BINDING_F3},
                                         {"f4", KEY_BINDING_F4},
                                         {"f5", KEY_BINDING_F5},
                                         {"f6", KEY_BINDING_F6},
                                         {"f7", KEY_BINDING_F7},
                                         {"f8", KEY_BINDING_F8},
                                         {"f9", KEY_BINDING_F9},
                                         {"f10", KEY_BINDING_F10},
                                         {"f11", KEY_BINDING_F11},
                                         {"f12", KEY_BINDING_F12}};

static const char *canonical_name(key_binding_code code)
{
    switch (code) {
    case KEY_BINDING_UP:
        return "up";
    case KEY_BINDING_DOWN:
        return "down";
    case KEY_BINDING_LEFT:
        return "left";
    case KEY_BINDING_RIGHT:
        return "right";
    case KEY_BINDING_HOME:
        return "home";
    case KEY_BINDING_END:
        return "end";
    case KEY_BINDING_PAGE_UP:
        return "pageup";
    case KEY_BINDING_PAGE_DOWN:
        return "pagedown";
    case KEY_BINDING_INSERT:
        return "insert";
    case KEY_BINDING_DELETE:
        return "delete";
    case KEY_BINDING_ENTER:
        return "enter";
    case KEY_BINDING_TAB:
        return "tab";
    case KEY_BINDING_BACKSPACE:
        return "backspace";
    case KEY_BINDING_ESCAPE:
        return "escape";
    case KEY_BINDING_F1:
        return "f1";
    case KEY_BINDING_F2:
        return "f2";
    case KEY_BINDING_F3:
        return "f3";
    case KEY_BINDING_F4:
        return "f4";
    case KEY_BINDING_F5:
        return "f5";
    case KEY_BINDING_F6:
        return "f6";
    case KEY_BINDING_F7:
        return "f7";
    case KEY_BINDING_F8:
        return "f8";
    case KEY_BINDING_F9:
        return "f9";
    case KEY_BINDING_F10:
        return "f10";
    case KEY_BINDING_F11:
        return "f11";
    case KEY_BINDING_F12:
        return "f12";
    default:
        return NULL;
    }
}

static const char *friendly_name(key_binding_code code)
{
    switch (code) {
    case KEY_BINDING_UP:
        return _("Seta ↑");
    case KEY_BINDING_DOWN:
        return _("Seta ↓");
    case KEY_BINDING_LEFT:
        return _("Seta ←");
    case KEY_BINDING_RIGHT:
        return _("Seta →");
    case KEY_BINDING_HOME:
        return _("Home");
    case KEY_BINDING_END:
        return _("End");
    case KEY_BINDING_PAGE_UP:
        return _("Page Up");
    case KEY_BINDING_PAGE_DOWN:
        return _("Page Down");
    case KEY_BINDING_INSERT:
        return _("Insert");
    case KEY_BINDING_DELETE:
        return _("Delete");
    case KEY_BINDING_ENTER:
        return _("Enter");
    case KEY_BINDING_TAB:
        return _("Tab");
    case KEY_BINDING_BACKSPACE:
        return _("Backspace");
    case KEY_BINDING_ESCAPE:
        return _("Esc");
    case KEY_BINDING_F1:
        return "F1";
    case KEY_BINDING_F2:
        return "F2";
    case KEY_BINDING_F3:
        return "F3";
    case KEY_BINDING_F4:
        return "F4";
    case KEY_BINDING_F5:
        return "F5";
    case KEY_BINDING_F6:
        return "F6";
    case KEY_BINDING_F7:
        return "F7";
    case KEY_BINDING_F8:
        return "F8";
    case KEY_BINDING_F9:
        return "F9";
    case KEY_BINDING_F10:
        return "F10";
    case KEY_BINDING_F11:
        return "F11";
    case KEY_BINDING_F12:
        return "F12";
    default:
        return NULL;
    }
}

static bool scalar_valid(unsigned code)
{
    return code <= 0x10ffff && !(code >= 0xd800 && code <= 0xdfff);
}

static bool parse_hex(const char *text, size_t length, unsigned *value)
{
    if (!length || length > 6)
        return false;
    unsigned result = 0;
    for (size_t i = 0; i < length; ++i) {
        char c = text[i];
        unsigned digit;
        if (c >= '0' && c <= '9')
            digit = (unsigned)(c - '0');
        else if (c >= 'a' && c <= 'f')
            digit = (unsigned)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F')
            digit = (unsigned)(c - 'A' + 10);
        else
            return false;
        result = result * 16 + digit;
    }
    *value = result;
    return true;
}

static bool parse_character(const char *text, unsigned *code)
{
    if (!strcmp(text, "space")) {
        *code = ' ';
        return true;
    }
    size_t length = strlen(text);
    if (length >= 5 && (text[0] == 'u' || text[0] == 'U') && parse_hex(text + 1, length - 1, code))
        return scalar_valid(*code);
    gunichar character = g_utf8_get_char_validated(text, (gssize)length);
    if (character == (gunichar)-1 || character == (gunichar)-2)
        return false;
    if (g_utf8_next_char(text) != text + length)
        return false;
    *code = (unsigned)character;
    return true;
}

static bool parse_modifier(const char *text, unsigned *modifiers)
{
    unsigned bit;
    if (!g_ascii_strcasecmp(text, "ctrl") || !g_ascii_strcasecmp(text, "control"))
        bit = KEY_BINDING_CTRL;
    else if (!g_ascii_strcasecmp(text, "alt") || !g_ascii_strcasecmp(text, "meta"))
        bit = KEY_BINDING_ALT;
    else if (!g_ascii_strcasecmp(text, "shift"))
        bit = KEY_BINDING_SHIFT;
    else
        return false;
    if (*modifiers & bit)
        return false;
    *modifiers |= bit;
    return true;
}

static bool parse_mouse(const char *text, unsigned *button)
{
    if (!g_ascii_strcasecmp(text, "mouse4")) {
        *button = 4;
        return true;
    }
    if (!g_ascii_strcasecmp(text, "mouse5")) {
        *button = 5;
        return true;
    }
    return false;
}

bool key_binding_parse(const char *text, key_binding *binding)
{
    if (!text || !binding || !*text || strlen(text) >= KEY_BINDING_TEXT)
        return false;
    char copy[KEY_BINDING_TEXT];
    snprintf(copy, sizeof(copy), "%s", text);
    char *parts[6];
    size_t count = 0;
    char *start = copy;
    for (;;) {
        if (count == 6)
            return false;
        parts[count++] = start;
        char *plus = strchr(start, '+');
        if (!plus)
            break;
        *plus = '\0';
        start = plus + 1;
        if (!*start)
            return false;
    }
    unsigned modifiers = 0;
    for (size_t i = 0; i + 1 < count; ++i)
        if (!parse_modifier(parts[i], &modifiers))
            return false;
    const char *base = parts[count - 1];
    key_binding result = {KEY_BINDING_NONE, 0, modifiers};
    if (parse_mouse(base, &result.code)) {
        result.kind = KEY_BINDING_MOUSE;
        result.modifiers = 0; /* Wheel modifiers are not reliable across terminals. */
    } else {
        for (size_t i = 0; i < sizeof(key_names) / sizeof(key_names[0]); ++i)
            if (!g_ascii_strcasecmp(base, key_names[i].name)) {
                result.kind = KEY_BINDING_KEY;
                result.code = key_names[i].code;
                break;
            }
        if (result.kind == KEY_BINDING_NONE) {
            if (!parse_character(base, &result.code))
                return false;
            result.kind = KEY_BINDING_CHAR;
        }
    }
    *binding = result;
    return true;
}

static bool append(char *output, size_t size, size_t *used, const char *text)
{
    size_t length = strlen(text);
    if (*used + length + 1 > size)
        return false;
    memcpy(output + *used, text, length + 1);
    *used += length;
    return true;
}

static bool format_character(unsigned code, char *output, size_t size, size_t *used)
{
    char buffer[16];
    if (code == ' ')
        snprintf(buffer, sizeof(buffer), "space");
    else if (code == '+')
        snprintf(buffer, sizeof(buffer), "u002b");
    else if (code >= 0x21 && code <= 0x7e)
        snprintf(buffer, sizeof(buffer), "%c", (int)code);
    else {
        int length = g_unichar_to_utf8((gunichar)code, buffer);
        if (length <= 0)
            return false;
        buffer[length] = 0;
    }
    return append(output, size, used, buffer);
}

static bool format_prefix(unsigned modifiers, char *output, size_t size, size_t *used)
{
    if ((modifiers & KEY_BINDING_CTRL) && !append(output, size, used, "ctrl+"))
        return false;
    if ((modifiers & KEY_BINDING_ALT) && !append(output, size, used, "alt+"))
        return false;
    if ((modifiers & KEY_BINDING_SHIFT) && !append(output, size, used, "shift+"))
        return false;
    return true;
}

bool key_binding_format(const key_binding *binding, char *output, size_t size)
{
    if (!binding || !output || !size || !key_binding_valid(binding))
        return false;
    size_t used = 0;
    output[0] = 0;
    if (!format_prefix(binding->modifiers, output, size, &used))
        return false;
    switch (binding->kind) {
    case KEY_BINDING_CHAR:
        return format_character(binding->code, output, size, &used);
    case KEY_BINDING_KEY: {
        const char *name = canonical_name((key_binding_code)binding->code);
        return name && append(output, size, &used, name);
    }
    case KEY_BINDING_MOUSE: {
        char buffer[16];
        snprintf(buffer, sizeof(buffer), "mouse%u", binding->code);
        return append(output, size, &used, buffer);
    }
    default:
        return false;
    }
}

bool key_binding_label(const key_binding *binding, char *output, size_t size)
{
    if (!binding || !output || !size || !key_binding_valid(binding))
        return false;
    size_t used = 0;
    output[0] = 0;
    if ((binding->modifiers & KEY_BINDING_CTRL) && !append(output, size, &used, _("Ctrl+")))
        return false;
    if ((binding->modifiers & KEY_BINDING_ALT) && !append(output, size, &used, _("Alt+")))
        return false;
    if ((binding->modifiers & KEY_BINDING_SHIFT) && !append(output, size, &used, _("Shift+")))
        return false;
    switch (binding->kind) {
    case KEY_BINDING_CHAR:
        if (binding->code == ' ')
            return append(output, size, &used, _("Espaço"));
        if (binding->code == '+')
            return append(output, size, &used, "+");
        {
            unsigned code = binding->code;
            if ((binding->modifiers & (KEY_BINDING_CTRL | KEY_BINDING_ALT | KEY_BINDING_SHIFT)) &&
                code >= 'a' && code <= 'z')
                code -= 'a' - 'A';
            char buffer[8];
            int length = g_unichar_to_utf8((gunichar)code, buffer);
            if (length <= 0)
                return false;
            buffer[length] = 0;
            return append(output, size, &used, buffer);
        }
    case KEY_BINDING_KEY: {
        const char *name = friendly_name((key_binding_code)binding->code);
        return name && append(output, size, &used, name);
    }
    case KEY_BINDING_MOUSE: {
        char buffer[16];
        snprintf(buffer, sizeof(buffer), _("Mouse %u"), binding->code);
        return append(output, size, &used, buffer);
    }
    default:
        return false;
    }
}

bool key_binding_valid(const key_binding *binding)
{
    if (!binding || binding->modifiers & ~(KEY_BINDING_SHIFT | KEY_BINDING_ALT | KEY_BINDING_CTRL))
        return false;
    switch (binding->kind) {
    case KEY_BINDING_CHAR:
        /* Terminal protocols report functional keys as private-use characters. */
        if (!scalar_valid(binding->code) || !g_unichar_isprint((gunichar)binding->code) ||
            binding->code < 0x20 || binding->code == 0x7f ||
            (binding->code >= 0x80 && binding->code <= 0x9f) ||
            (binding->code >= 0xe000 && binding->code <= 0xf8ff))
            return false;
        /* Printable keys always need Ctrl or Alt. Shift alone cannot be
         * distinguished from ordinary shifted typing by the terminal. */
        return (binding->modifiers & (KEY_BINDING_CTRL | KEY_BINDING_ALT)) != 0;
    case KEY_BINDING_KEY:
        if (binding->code < KEY_BINDING_UP || binding->code > KEY_BINDING_F12)
            return false;
        if (binding->code == KEY_BINDING_ESCAPE || binding->code == KEY_BINDING_ENTER ||
            binding->code == KEY_BINDING_TAB || binding->code == KEY_BINDING_BACKSPACE)
            return binding->modifiers != 0;
        return true;
    case KEY_BINDING_MOUSE:
        return (binding->code == 4 || binding->code == 5) && !binding->modifiers;
    default:
        return false;
    }
}

const char *key_binding_error(const key_binding *binding)
{
    if (key_binding_valid(binding))
        return NULL;
    if (!binding)
        return _("Combinação de teclas inválida.");
    if (binding->kind == KEY_BINDING_CHAR)
        return _("Use Ctrl ou Alt com letras, números, espaço e símbolos.");
    if (binding->kind == KEY_BINDING_KEY && binding->code == KEY_BINDING_ESCAPE)
        return _("Esc sozinho fecha painéis; use Ctrl, Alt ou Shift com Esc.");
    if (binding->kind == KEY_BINDING_KEY &&
        (binding->code == KEY_BINDING_ENTER || binding->code == KEY_BINDING_TAB ||
         binding->code == KEY_BINDING_BACKSPACE))
        return _("Use Ctrl, Alt ou Shift com Enter, Tab ou Backspace.");
    return _("Combinação de teclas inválida.");
}

bool key_binding_equal(const key_binding *left, const key_binding *right)
{
    return left && right && left->kind == right->kind && left->code == right->code &&
           left->modifiers == right->modifiers;
}

unsigned key_binding_legacy_ctrl_byte(const key_binding *binding)
{
    if (!binding || binding->kind != KEY_BINDING_CHAR || !(binding->modifiers & KEY_BINDING_CTRL) ||
        (binding->modifiers & KEY_BINDING_ALT))
        return 0;
    unsigned code = binding->code, value = 0;
    if (code >= 'a' && code <= 'z')
        value = code - 'a' + 1;
    else if (code >= 'A' && code <= 'Z')
        value = code - 'A' + 1;
    else if (code == '\\')
        value = 0x1c;
    else if (code == ']')
        value = 0x1d;
    else if (code == '^')
        value = 0x1e;
    else if (code == '_' || code == '/')
        value = 0x1f;
    /* Backspace, Tab, Enter and Escape have their own terminal encodings, so
     * their control bytes cannot identify a push-to-talk binding. */
    if (value == 0x08 || value == 0x09 || value == 0x0a || value == 0x0d || value == 0x1b)
        return 0;
    return value;
}

bool key_binding_legacy(unsigned function_key, key_binding *binding)
{
    if (!binding || function_key < 2 || function_key > 12)
        return false;
    binding->kind = KEY_BINDING_KEY;
    binding->code = KEY_BINDING_F1 + function_key - 1;
    binding->modifiers = KEY_BINDING_CTRL;
    return true;
}

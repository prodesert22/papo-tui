#include "ui/input/shortcuts.h"

#include <ncurses.h>
#include <stdatomic.h>

_Static_assert(KEYMAP_ACTIONS <= TEXT_BINDING_BASE - TEXT_ACTION_BASE,
               "every command needs a synthetic event code");

static key_binding make(key_binding_kind kind, unsigned code, unsigned modifiers)
{
    key_binding binding = {kind, code, modifiers};
    return binding;
}

static bool control_byte(uint32_t key, key_binding *binding)
{
    switch (key) {
    case 0:
        *binding = make(KEY_BINDING_CHAR, ' ', KEY_BINDING_CTRL);
        return true;
    case 8:
    case 127:
        *binding = make(KEY_BINDING_KEY, KEY_BINDING_BACKSPACE, 0);
        return true;
    case 9:
        *binding = make(KEY_BINDING_KEY, KEY_BINDING_TAB, 0);
        return true;
    case 13:
        *binding = make(KEY_BINDING_KEY, KEY_BINDING_ENTER, 0);
        return true;
    case 27:
        *binding = make(KEY_BINDING_KEY, KEY_BINDING_ESCAPE, 0);
        return true;
    case 28:
        *binding = make(KEY_BINDING_CHAR, '\\', KEY_BINDING_CTRL);
        return true;
    case 29:
        *binding = make(KEY_BINDING_CHAR, ']', KEY_BINDING_CTRL);
        return true;
    case 30:
        *binding = make(KEY_BINDING_CHAR, '^', KEY_BINDING_CTRL);
        keymap_binding_normalize(binding);
        return true;
    case 31:
        /* Legacy Ctrl+_ and Ctrl+/ share this byte; CSI-u reports Ctrl+/ here. */
        *binding = make(KEY_BINDING_CHAR, '/', KEY_BINDING_CTRL);
        return true;
    default:
        if (key >= 1 && key <= 26) {
            *binding = make(KEY_BINDING_CHAR, 'a' + key - 1, KEY_BINDING_CTRL);
            return true;
        }
        return false;
    }
}

static bool function_key(uint32_t key, key_binding *binding)
{
    if (key < (uint32_t)KEY_F(1) || key > (uint32_t)KEY_F(63))
        return false;
    /* xterm terminfo numbers modified function keys in groups of twelve:
     * Shift, Ctrl, Ctrl+Shift, Alt and Alt+Shift. */
    static const unsigned groups[] = {0,
                                      KEY_BINDING_SHIFT,
                                      KEY_BINDING_CTRL,
                                      KEY_BINDING_CTRL | KEY_BINDING_SHIFT,
                                      KEY_BINDING_ALT,
                                      KEY_BINDING_ALT | KEY_BINDING_SHIFT};
    unsigned index = key - (uint32_t)KEY_F(1);
    *binding = make(KEY_BINDING_KEY, KEY_BINDING_F1 + index % 12, groups[index / 12]);
    return true;
}

static bool application_key(uint32_t key, key_binding *binding)
{
    static const key_binding_code directions[] = {KEY_BINDING_LEFT, KEY_BINDING_RIGHT,
                                                  KEY_BINDING_UP,   KEY_BINDING_DOWN,
                                                  KEY_BINDING_HOME, KEY_BINDING_END};
    if (key >= TEXT_SHIFT_LEFT && key <= TEXT_CTRL_SHIFT_END) {
        static const unsigned groups[] = {KEY_BINDING_SHIFT, KEY_BINDING_CTRL,
                                          KEY_BINDING_CTRL | KEY_BINDING_SHIFT};
        unsigned index = key - TEXT_SHIFT_LEFT;
        *binding = make(KEY_BINDING_KEY, directions[index % 6], groups[index / 6]);
        return true;
    }
    if (key >= TEXT_ALT_LEFT && key <= TEXT_ALT_DOWN) {
        *binding = make(KEY_BINDING_KEY, directions[key - TEXT_ALT_LEFT], KEY_BINDING_ALT);
        return true;
    }
    switch (key) {
    case TEXT_COPY:
        *binding = make(KEY_BINDING_CHAR, 'c', KEY_BINDING_CTRL | KEY_BINDING_SHIFT);
        return true;
    case TEXT_SHIFT_ENTER:
        *binding = make(KEY_BINDING_KEY, KEY_BINDING_ENTER, KEY_BINDING_SHIFT);
        return true;
    case TEXT_CTRL_BACKSPACE:
        *binding = make(KEY_BINDING_KEY, KEY_BINDING_BACKSPACE, KEY_BINDING_CTRL);
        return true;
    case TEXT_SUPER_BACKSPACE:
        *binding = make(KEY_BINDING_KEY, KEY_BINDING_BACKSPACE, KEY_BINDING_SUPER);
        return true;
    case TEXT_SHIFT_PAGE_UP:
        *binding = make(KEY_BINDING_KEY, KEY_BINDING_PAGE_UP, KEY_BINDING_SHIFT);
        return true;
    case TEXT_SHIFT_PAGE_DOWN:
        *binding = make(KEY_BINDING_KEY, KEY_BINDING_PAGE_DOWN, KEY_BINDING_SHIFT);
        return true;
    default:
        return text_binding_key(key, binding);
    }
}

static bool curses_key(uint32_t key, key_binding *binding)
{
    static const struct {
        int curses;
        key_binding_code code;
        unsigned modifiers;
    } keys[] = {
        {KEY_UP, KEY_BINDING_UP, 0},
        {KEY_DOWN, KEY_BINDING_DOWN, 0},
        {KEY_LEFT, KEY_BINDING_LEFT, 0},
        {KEY_RIGHT, KEY_BINDING_RIGHT, 0},
        {KEY_HOME, KEY_BINDING_HOME, 0},
        {KEY_END, KEY_BINDING_END, 0},
        {KEY_PPAGE, KEY_BINDING_PAGE_UP, 0},
        {KEY_NPAGE, KEY_BINDING_PAGE_DOWN, 0},
        {KEY_IC, KEY_BINDING_INSERT, 0},
        {KEY_DC, KEY_BINDING_DELETE, 0},
        {KEY_ENTER, KEY_BINDING_ENTER, 0},
        {KEY_BACKSPACE, KEY_BINDING_BACKSPACE, 0},
        {KEY_BTAB, KEY_BINDING_TAB, KEY_BINDING_SHIFT},
        {KEY_SLEFT, KEY_BINDING_LEFT, KEY_BINDING_SHIFT},
        {KEY_SRIGHT, KEY_BINDING_RIGHT, KEY_BINDING_SHIFT},
        {KEY_SR, KEY_BINDING_UP, KEY_BINDING_SHIFT},
        {KEY_SF, KEY_BINDING_DOWN, KEY_BINDING_SHIFT},
        {KEY_SHOME, KEY_BINDING_HOME, KEY_BINDING_SHIFT},
        {KEY_SEND, KEY_BINDING_END, KEY_BINDING_SHIFT},
    };
    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i)
        if (key == (uint32_t)keys[i].curses) {
            *binding = make(KEY_BINDING_KEY, keys[i].code, keys[i].modifiers);
            return true;
        }
    return function_key(key, binding);
}

bool shortcut_event(int type, uint32_t key, key_binding *binding)
{
    if (!binding)
        return false;
    if (type == OK) {
        if (control_byte(key, binding))
            return true;
        *binding = make(KEY_BINDING_CHAR, key, 0);
        return key_binding_well_formed(binding);
    }
    if (type != KEY_CODE_YES)
        return false;
    return curses_key(key, binding) || application_key(key, binding);
}

uint32_t shortcut_code(keymap_action action)
{
    return TEXT_ACTION_BASE + (uint32_t)action;
}

bool shortcut_is(int type, uint32_t key, keymap_action action)
{
    if (action <= KEYMAP_NONE || action >= KEYMAP_ACTIONS)
        return false;
    if (type == KEY_CODE_YES && key >= TEXT_ACTION_BASE && key < TEXT_BINDING_BASE)
        return key == shortcut_code(action);
    key_binding binding;
    return shortcut_event(type, key, &binding) && keymap_matches(action, &binding);
}

/* Keys the base decoder reports by itself, as a control byte, a curses key or
 * one of the TEXT_* codes; every other key must be registered. */
static bool decoded_natively(const key_binding *key)
{
    unsigned modifiers = key->modifiers;
    /* Only the kitty keyboard protocol reports Super (Command on macOS). */
    if (modifiers & KEY_BINDING_SUPER)
        return false;
    if (key->kind == KEY_BINDING_CHAR) {
        if (!modifiers)
            return true;
        if (modifiers == (KEY_BINDING_CTRL | KEY_BINDING_SHIFT))
            return key->code == 'c';
        if (modifiers != KEY_BINDING_CTRL)
            return false;
        unsigned code = key->code;
        return (code >= 'a' && code <= 'z') || code == ' ' || code == '\\' || code == ']' ||
               code == '/';
    }
    if (key->kind != KEY_BINDING_KEY)
        return false;
    if (!modifiers)
        return true;
    switch (key->code) {
    case KEY_BINDING_UP:
    case KEY_BINDING_DOWN:
    case KEY_BINDING_LEFT:
    case KEY_BINDING_RIGHT:
        return true; /* Shift, Ctrl, Ctrl+Shift and Alt arrows are all decoded. */
    case KEY_BINDING_HOME:
    case KEY_BINDING_END:
        return !(modifiers & KEY_BINDING_ALT);
    case KEY_BINDING_TAB:
    case KEY_BINDING_ENTER:
    case KEY_BINDING_PAGE_UP:
    case KEY_BINDING_PAGE_DOWN:
        return modifiers == KEY_BINDING_SHIFT;
    case KEY_BINDING_BACKSPACE:
        return modifiers == KEY_BINDING_CTRL;
    default:
        return false;
    }
}

void shortcut_refresh(void)
{
    static atomic_uint_fast64_t registered;
    uint64_t revision = keymap_revision();
    if (atomic_load(&registered) == revision)
        return;
    atomic_store(&registered, revision);
    keymap_table table;
    keymap_current(&table);
    key_binding keys[TEXT_BINDING_END - TEXT_BINDING_BASE];
    size_t count = 0;
    for (int action = 0; action < KEYMAP_ACTIONS; ++action)
        for (unsigned i = 0; i < table.count[action]; ++i) {
            const key_binding *key = &table.keys[action][i];
            if (decoded_natively(key))
                continue;
            bool known = false;
            for (size_t j = 0; j < count && !known; ++j)
                known = key_binding_equal(&keys[j], key);
            if (!known && count < sizeof(keys) / sizeof(keys[0]))
                keys[count++] = *key;
        }
    text_bindings_configure(keys, count);
}

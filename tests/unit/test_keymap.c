#define _POSIX_C_SOURCE 200809L
#include "config/keymap.h"
#include "ui/input/shortcuts.h"
#include <assert.h>
#include <ncurses.h>
#include <stdio.h>
#include <string.h>
#ifndef _WIN32
#include <unistd.h>
#endif

static key_binding parse(const char *text)
{
    key_binding key;
    assert(key_binding_parse(text, &key));
    keymap_binding_normalize(&key);
    return key;
}

static void test_defaults_and_labels(void)
{
    keymap_table table;
    keymap_table_defaults(&table);
    keymap_apply(&table);
    char label[128];
    assert(keymap_label(KEYMAP_HELP, false, label, sizeof(label)));
    assert(!strcmp(label, "Ctrl+G / F1"));
    assert(keymap_label(KEYMAP_HELP, true, label, sizeof(label)));
    assert(!strcmp(label, "Ctrl+G"));
    assert(keymap_label(KEYMAP_MENU, false, label, sizeof(label)));
    assert(!strcmp(label, "Ctrl+Espaço"));
    assert(keymap_label(KEYMAP_MEDIA_INFO, false, label, sizeof(label)));
    assert(!strcmp(label, "Ctrl+B / i"));
    assert(!strcmp(keymap_text("{k1:app.help}: ajuda"), "Ctrl+G: ajuda"));
    assert(
        !strcmp(keymap_text("{k:selection.reply} / {k:selection.react} / {k:selection.reactors}"),
                "r / e / E"));
    /* Unknown ids stay visible instead of disappearing. */
    assert(!strcmp(keymap_text("{k:missing.action} x"), "{k:missing.action} x"));
    for (int i = 0; i < KEYMAP_ACTIONS; ++i) {
        keymap_action action = (keymap_action)i;
        assert(keymap_action_find(keymap_action_id(action)) == action);
        assert(*keymap_action_label(action));
        /* Every default is a key its command accepts. */
        for (unsigned j = 0; j < table.count[i]; ++j)
            assert(!keymap_binding_error(action, &table.keys[i][j]));
    }
}

static void test_normalize_and_errors(void)
{
    key_binding key = parse("ctrl+K");
    assert(key.code == 'k' && key.modifiers == (KEY_BINDING_CTRL | KEY_BINDING_SHIFT));
    key = parse("shift+e");
    assert(key.code == 'E' && !key.modifiers);
    key = parse("shift+,");
    assert(key.code == '<' && !key.modifiers);
    key = parse("alt+?");
    assert(key.code == '/' && key.modifiers == (KEY_BINDING_ALT | KEY_BINDING_SHIFT));
    assert(keymap_binding_error(KEYMAP_SWITCHER, &(key_binding){KEY_BINDING_CHAR, 'k', 0}));
    assert(!keymap_binding_error(KEYMAP_REPLY, &(key_binding){KEY_BINDING_CHAR, 'q', 0}));
    assert(keymap_binding_error(KEYMAP_REPLY, &(key_binding){KEY_BINDING_CHAR, ' ', 0}));
    key = parse("ctrl+h");
    assert(keymap_binding_error(KEYMAP_SWITCHER, &key));
    key = parse("ctrl+v");
    assert(keymap_binding_error(KEYMAP_SWITCHER, &key));
    key = parse("ctrl+shift+c");
    assert(keymap_binding_error(KEYMAP_SWITCHER, &key));
    key = parse("enter");
    assert(keymap_binding_error(KEYMAP_REPLY, &key));
    key = parse("alt+left");
    assert(keymap_binding_error(KEYMAP_SWITCHER, &key));
    key = parse("mouse4");
    assert(keymap_binding_error(KEYMAP_SWITCHER, &key));
    key = parse("alt+k");
    assert(!keymap_binding_error(KEYMAP_SWITCHER, &key));
    key = parse("ctrl+f5");
    assert(!keymap_binding_error(KEYMAP_SWITCHER, &key));
    key = parse("ctrl+j");
    assert(!keymap_binding_error(KEYMAP_SELECT, &key));
}

static void test_overrides(void)
{
    json_error_t error;
    json_t *object = json_loads("{\"conversation.switcher\": [\"alt+k\", \"F3\"],"
                                " \"selection.reply\": [],"
                                " \"future.action\": [\"ctrl+alt+z\"]}",
                                0, &error);
    keymap_overrides overrides;
    assert(object && keymap_overrides_decode(&overrides, object));
    json_decref(object);
    assert(overrides.count == 3);
    keymap_table table;
    keymap_table_from_overrides(&table, &overrides);
    assert(table.count[KEYMAP_SWITCHER] == 2 && table.count[KEYMAP_REPLY] == 0);
    assert(table.keys[KEYMAP_SWITCHER][0].modifiers == KEY_BINDING_ALT);
    /* Only what differs is stored; unknown ids from a newer version survive. */
    table.count[KEYMAP_REPLY] = 1;
    table.keys[KEYMAP_REPLY][0] = (key_binding){KEY_BINDING_CHAR, 'r', 0};
    keymap_overrides saved;
    keymap_table_to_overrides(&table, &overrides, &saved);
    assert(saved.count == 2);
    assert(!strcmp(saved.items[0].id, "conversation.switcher"));
    assert(!strcmp(saved.items[1].id, "future.action"));
    json_t *encoded = keymap_overrides_encode(&saved);
    char *text = json_dumps(encoded, JSON_SORT_KEYS | JSON_COMPACT);
    assert(!strcmp(text, "{\"conversation.switcher\":[\"alt+k\",\"f3\"],"
                         "\"future.action\":[\"ctrl+alt+z\"]}"));
    free(text);
    json_decref(encoded);

    static const char *refused[] = {
        "[]",
        "{\"app.quit\": []}",
        "{\"conversation.switcher\": [\"k\"]}",
        "{\"conversation.switcher\": [\"alt+k\", \"alt+k\"]}",
        "{\"conversation.switcher\": [\"alt+k\", \"f2\", \"f3\", \"f4\"]}",
        "{\"conversation.switcher\": [\"nope+k\"]}",
        "{\"conversation.switcher\": \"alt+k\"}",
        "{\"Bad Id\": []}",
        "{\"selection.reply\": [\"enter\"]}",
    };
    for (size_t i = 0; i < sizeof(refused) / sizeof(refused[0]); ++i) {
        object = json_loads(refused[i], JSON_DECODE_ANY, &error);
        assert(object && !keymap_overrides_decode(&overrides, object));
        json_decref(object);
    }
}

static void test_apply_and_text(void)
{
    keymap_table table;
    keymap_table_defaults(&table);
    keymap_apply(&table);
    uint64_t before = keymap_revision();
    const char *old = keymap_text("{k1:app.help}: ajuda");
    table.keys[KEYMAP_HELP][0] = parse("f2");
    table.count[KEYMAP_HELP] = 1;
    table.keys[KEYMAP_MUTE][0] = (key_binding){KEY_BINDING_CHAR, '%', 0};
    keymap_apply(&table);
    assert(keymap_revision() != before);
    assert(!strcmp(keymap_text("{k1:app.help}: ajuda"), "F2: ajuda"));
    /* A text taken before the change stays readable. */
    assert(!strcmp(old, "Ctrl+G: ajuda"));
    /* In a format, a % in a label must not become a conversion. */
    assert(!strcmp(keymap_text("{k:player.mute} %s"), "%% %s"));
    assert(!strcmp(keymap_text("{k:player.mute} x"), "% x"));
    keymap_apply(&table);
    key_binding key = parse("M");
    assert(keymap_matches(KEYMAP_ITEM_MENU, &key));
    key = parse("E");
    assert(keymap_matches(KEYMAP_REACTORS, &key) && !keymap_matches(KEYMAP_REACT, &key));
    keymap_table_defaults(&table);
    keymap_apply(&table);
}

static void test_conflicts(void)
{
    keymap_table table;
    keymap_table_defaults(&table);
    /* The defaults never collide where both commands listen. */
    for (int i = 0; i < KEYMAP_ACTIONS; ++i)
        for (unsigned j = 0; j < table.count[i]; ++j)
            assert(keymap_find_conflict(&table, (keymap_action)i, &table.keys[i][j]).kind ==
                   KEYMAP_CONFLICT_NONE);
    key_binding key = parse("ctrl+b");
    keymap_conflict found = keymap_find_conflict(&table, KEYMAP_SWITCHER, &key);
    assert(found.kind == KEYMAP_CONFLICT_ACTION && found.action == KEYMAP_PINNED && found.label);
    /* Areas that never listen together may share a key. */
    key = parse("ctrl+e");
    assert(keymap_find_conflict(&table, KEYMAP_MUTE, &key).kind == KEYMAP_CONFLICT_NONE);
    /* Application-wide keys reach every area. */
    key = parse("ctrl+q");
    found = keymap_find_conflict(&table, KEYMAP_MUTE, &key);
    assert(found.kind == KEYMAP_CONFLICT_ACTION && found.action == KEYMAP_QUIT);
    /* Editing keys of the areas where the command listens. */
    key = parse("ctrl+w");
    assert(keymap_find_conflict(&table, KEYMAP_SWITCHER, &key).kind == KEYMAP_CONFLICT_FIXED);
    key = parse("ctrl+u");
    assert(keymap_find_conflict(&table, KEYMAP_REGISTER, &key).kind == KEYMAP_CONFLICT_FIXED);
    /* A caseless letter also takes the other case. */
    key = parse("M");
    found = keymap_find_conflict(&table, KEYMAP_REPLY, &key);
    assert(found.kind == KEYMAP_CONFLICT_ACTION && found.action == KEYMAP_ITEM_MENU);
    assert(keymap_conversation_action(KEYMAP_RETRY_SEND) &&
           keymap_conversation_action(KEYMAP_QUIT));
    assert(!keymap_conversation_action(KEYMAP_MUTE) &&
           !keymap_conversation_action(KEYMAP_PROFILE_ADD));
    /* Every command belongs to an area and the groups are consecutive. */
    int groups = 1;
    for (int i = 1; i < KEYMAP_ACTIONS; ++i) {
        assert(*keymap_action_group((keymap_action)i));
        groups +=
            keymap_action_group((keymap_action)i) != keymap_action_group((keymap_action)(i - 1));
    }
    assert(groups == KEYMAP_GROUPS);
    assert(keymap_fixed_count() > 0);
}

static void test_events(void)
{
    key_binding key;
    assert(shortcut_event(OK, 11, &key) && key.kind == KEY_BINDING_CHAR && key.code == 'k' &&
           key.modifiers == KEY_BINDING_CTRL);
    assert(shortcut_event(OK, 0, &key) && key.code == ' ' && key.modifiers == KEY_BINDING_CTRL);
    assert(shortcut_event(OK, 31, &key) && key.code == '/' && key.modifiers == KEY_BINDING_CTRL);
    assert(shortcut_event(OK, 'E', &key) && key.code == 'E' && !key.modifiers);
    assert(shortcut_event(KEY_CODE_YES, KEY_F(5), &key) && key.kind == KEY_BINDING_KEY &&
           key.code == KEY_BINDING_F5 && !key.modifiers);
    assert(shortcut_event(KEY_CODE_YES, KEY_F(17), &key) && key.code == KEY_BINDING_F5 &&
           key.modifiers == KEY_BINDING_SHIFT);
    assert(shortcut_event(KEY_CODE_YES, KEY_F(29), &key) && key.code == KEY_BINDING_F5 &&
           key.modifiers == KEY_BINDING_CTRL);
    assert(shortcut_event(KEY_CODE_YES, TEXT_ALT_UP, &key) && key.code == KEY_BINDING_UP &&
           key.modifiers == KEY_BINDING_ALT);
    assert(shortcut_event(KEY_CODE_YES, TEXT_COPY, &key) && key.code == 'c' &&
           key.modifiers == (KEY_BINDING_CTRL | KEY_BINDING_SHIFT));
    assert(!shortcut_event(KEY_CODE_YES, TEXT_PASTE_START, &key));
    assert(shortcut_is(OK, 7, KEYMAP_HELP) && shortcut_is(KEY_CODE_YES, KEY_F(1), KEYMAP_HELP));
    assert(!shortcut_is(OK, 7, KEYMAP_HELP_ALL) && shortcut_is(OK, 31, KEYMAP_HELP_ALL));
    assert(shortcut_is(OK, 'm', KEYMAP_ITEM_MENU) && shortcut_is(OK, 'M', KEYMAP_ITEM_MENU));
    assert(shortcut_is(OK, 'm', KEYMAP_MUTE) && !shortcut_is(OK, 'M', KEYMAP_MUTE));
    assert(shortcut_is(OK, 10, KEYMAP_SELECT));
    assert(shortcut_is(KEY_CODE_YES, shortcut_code(KEYMAP_SELECT), KEYMAP_SELECT));
    assert(!shortcut_is(KEY_CODE_YES, shortcut_code(KEYMAP_SELECT), KEYMAP_MENU));
    assert(!shortcut_is(OK, 10, KEYMAP_NONE));
}

static void test_super(void)
{
    /* Command on macOS, the Super key elsewhere, as the kitty protocol bit 8. */
    key_binding key = parse("cmd+S");
    assert(key.code == 's' && key.modifiers == (KEY_BINDING_SUPER | KEY_BINDING_SHIFT));
    key = parse("command+/");
    assert(key.code == '/' && key.modifiers == KEY_BINDING_SUPER);
    key = parse("ctrl+alt+shift+super+pagedown");
    char text[KEY_BINDING_TEXT];
    assert(key_binding_format(&key, text, sizeof(text)) &&
           !strcmp(text, "ctrl+alt+shift+super+pagedown"));
    key = parse("super+s");
    assert(key_binding_format(&key, text, sizeof(text)) && !strcmp(text, "super+s"));
    assert(key_binding_parse("super+super+s", &key) == false);
    key = parse("super+s");
    char label[64];
    assert(key_binding_label(&key, label, sizeof(label)));
#ifdef __APPLE__
    assert(!strcmp(label, "Cmd+S"));
#else
    assert(!strcmp(label, "Super+S"));
#endif
    /* A Super letter is not typed, so commands that act in text fields take it. */
    assert(!keymap_binding_error(KEYMAP_SWITCHER, &key));
    assert(key_binding_valid(&key) && !key_binding_error(&key));
    key = parse("ctrl+super+a");
    assert(!key_binding_legacy_ctrl_byte(&key) && !keymap_binding_error(KEYMAP_SWITCHER, &key));
    assert(key_binding_well_formed(
        &(key_binding){KEY_BINDING_KEY, KEY_BINDING_UP, KEY_BINDING_SUPER}));
    assert(!key_binding_well_formed(&(key_binding){KEY_BINDING_CHAR, 's', 16}));

    json_error_t error;
    json_t *object =
        json_loads("{\"conversation.switcher\": [\"cmd+k\", \"super+shift+k\"]}", 0, &error);
    keymap_overrides overrides;
    assert(object && keymap_overrides_decode(&overrides, object));
    json_decref(object);
    assert(overrides.items[0].keys[0].modifiers == KEY_BINDING_SUPER);
    json_t *encoded = keymap_overrides_encode(&overrides);
    char *dumped = json_dumps(encoded, JSON_COMPACT);
    assert(!strcmp(dumped, "{\"conversation.switcher\":[\"super+k\",\"shift+super+k\"]}"));
    free(dumped);
    json_decref(encoded);
}

static void test_platform_defaults(void)
{
    keymap_table others, apple;
    keymap_table_platform_defaults(&others, false);
    keymap_table_platform_defaults(&apple, true);
    /* Linux and Windows keep one key; macOS adds Command after Ctrl. */
    assert(others.count[KEYMAP_SETTINGS] == 1);
    assert(apple.count[KEYMAP_SETTINGS] == 2 &&
           key_binding_equal(&apple.keys[KEYMAP_SETTINGS][0], &others.keys[KEYMAP_SETTINGS][0]) &&
           key_binding_equal(&apple.keys[KEYMAP_SETTINGS][1],
                             &(key_binding){KEY_BINDING_CHAR, 's', KEY_BINDING_SUPER}));
    /* Command+K, F, Q and the other keys of the terminals stay with them. */
    assert(apple.count[KEYMAP_SWITCHER] == 1 && apple.count[KEYMAP_SEARCH] == 1 &&
           apple.count[KEYMAP_QUIT] == 1 && apple.count[KEYMAP_RECONNECT] == 1);
    for (int i = 0; i < KEYMAP_ACTIONS; ++i) {
        assert(apple.count[i] >= others.count[i]);
        for (unsigned j = 0; j < others.count[i]; ++j)
            assert(key_binding_equal(&apple.keys[i][j], &others.keys[i][j]));
        for (unsigned j = 0; j < apple.count[i]; ++j) {
            assert(!keymap_binding_error((keymap_action)i, &apple.keys[i][j]));
            assert(keymap_find_conflict(&apple, (keymap_action)i, &apple.keys[i][j]).kind ==
                   KEYMAP_CONFLICT_NONE);
        }
    }
    keymap_table built;
    keymap_table_defaults(&built);
#ifdef __APPLE__
    assert(!memcmp(&built, &apple, sizeof(built)));
#else
    assert(!memcmp(&built, &others, sizeof(built)));
#endif
    /* Only what differs from this platform's defaults is stored. */
    keymap_overrides saved;
    keymap_table_to_overrides(&built, NULL, &saved);
    assert(!saved.count);
    keymap_apply(&apple);
    char label[128];
    assert(keymap_label(KEYMAP_SETTINGS, false, label, sizeof(label)));
#ifdef __APPLE__
    assert(!strcmp(label, "Ctrl+S / Cmd+S"));
#else
    assert(!strcmp(label, "Ctrl+S / Super+S"));
#endif
    assert(keymap_label(KEYMAP_SETTINGS, true, label, sizeof(label)) && !strcmp(label, "Ctrl+S"));
    keymap_apply(&built);
}

/* The Windows ncurses backend cannot create a screen from tmpfile streams. */
#ifndef _WIN32
/* Reads the next key a terminal sends, through the application's decoder. */
static int read_key(int terminal, const char *bytes, uint32_t *key)
{
    size_t length = strlen(bytes);
    assert(write(terminal, bytes, length) == (ssize_t)length);
    return text_input_read(key);
}

static void test_super_decoding(void)
{
    int ends[2];
    assert(!pipe(ends));
    FILE *input = fdopen(ends[0], "r"), *output = tmpfile();
    assert(input && output);
    SCREEN *screen = newterm("xterm-256color", output, input);
    assert(screen);
    raw();
    noecho();
    keypad(stdscr, TRUE);
    timeout(0);
    set_escdelay(25);
    text_input_init();
    keymap_table table;
    keymap_table_platform_defaults(&table, true);
    keymap_apply(&table);

    /* Kitty, Ghostty and WezTerm report Command+S as CSI 115 ; 1+8 u. */
    uint32_t key;
    int type = read_key(ends[1], "\033[115;9u", &key);
    assert(type == KEY_CODE_YES && shortcut_is(type, key, KEYMAP_SETTINGS));
    key_binding binding;
    assert(shortcut_event(type, key, &binding) && binding.code == 's' &&
           binding.modifiers == KEY_BINDING_SUPER);
    type = read_key(ends[1], "\033[47;9u", &key);
    assert(type == KEY_CODE_YES && shortcut_is(type, key, KEYMAP_HELP_ALL));
    /* Ctrl+S keeps its control byte. */
    type = read_key(ends[1], "\023", &key);
    assert(type == OK && shortcut_is(type, key, KEYMAP_SETTINGS));
    /* With press and release events for push-to-talk, releases are consumed too. */
    text_input_terminal_active(true);
    assert(read_key(ends[1], "\033[?1u", &key) == ERR && text_keyboard_events_supported());
    text_input_event_reporting(true);
    type = read_key(ends[1], "\033[115;9:1u", &key);
    assert(type == KEY_CODE_YES && shortcut_is(type, key, KEYMAP_SETTINGS));
    type = read_key(ends[1], "\033[115;9:3u", &key);
    assert(type == KEY_CODE_YES && key == TEXT_IGNORED);
    text_input_event_reporting(false);
    text_input_terminal_active(false);

    /* Settings → Atalhos captures Command with the kitty protocol flags. */
    assert(text_capture_begin());
    type = read_key(ends[1], "\033[115;10u", &key);
    assert(type == KEY_CODE_YES && key == TEXT_CAPTURED && text_capture_take(&binding));
    assert(binding.kind == KEY_BINDING_CHAR && binding.code == 's' &&
           binding.modifiers == (KEY_BINDING_SUPER | KEY_BINDING_SHIFT));
    assert(text_capture_begin());
    type = read_key(ends[1], "\033[57352;9u", &key);
    assert(type == KEY_CODE_YES && key == TEXT_CAPTURED && text_capture_take(&binding));
    assert(binding.kind == KEY_BINDING_KEY && binding.code == KEY_BINDING_UP &&
           binding.modifiers == KEY_BINDING_SUPER);
    /* In xterm's modifyOtherKeys bit 8 is Meta, never Command. */
    assert(text_capture_begin());
    type = read_key(ends[1], "\033[27;13;107~", &key);
    assert(type == KEY_CODE_YES && key == TEXT_CAPTURED && text_capture_take(&binding));
    assert(binding.code == 'k' && binding.modifiers == KEY_BINDING_CTRL);

    keymap_table_defaults(&table);
    keymap_apply(&table);
    endwin();
    delscreen(screen);
    fclose(input);
    fclose(output);
    close(ends[1]);
}
#endif

int main(void)
{
    language_set(LANGUAGE_PORTUGUESE);
    test_defaults_and_labels();
    test_normalize_and_errors();
    test_overrides();
    test_apply_and_text();
    test_conflicts();
    test_events();
    test_super();
    test_platform_defaults();
#ifndef _WIN32
    test_super_decoding();
#endif
    keymap_shutdown();
    return 0;
}

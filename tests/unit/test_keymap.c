#include "config/keymap.h"
#include "ui/input/shortcuts.h"
#include <assert.h>
#include <ncurses.h>
#include <string.h>

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
    assert(!strcmp(keymap_text("{k:selection.reply} / {k:selection.react} / {k:selection.reactors}"),
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

int main(void)
{
    language_set(LANGUAGE_PORTUGUESE);
    test_defaults_and_labels();
    test_normalize_and_errors();
    test_overrides();
    test_apply_and_text();
    test_events();
    keymap_shutdown();
    return 0;
}

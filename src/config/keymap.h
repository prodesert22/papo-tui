#ifndef PAPO_KEYMAP_H
#define PAPO_KEYMAP_H

#include "config/key_binding.h"
#include "model/text/translation.h"
#include <jansson.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Keys of the application's commands. Every command has a stable id that the
 * configuration stores, the areas where it listens, its default keys and the
 * keys in effect. Navigation, confirmation and text editing keys (arrows,
 * Enter, Esc, Tab, Backspace and the editing controls) are not commands: they
 * stay fixed so a remapping can never lock anyone out of a screen. */
#define KEYMAP_BINDINGS 3
#define KEYMAP_ID_BYTES 48
#define KEYMAP_OVERRIDES 128

/* Areas that hear a command. Areas inside the conversation screen also hear
 * the conversation-wide commands; every area hears the application-wide ones.
 * Modal panels consume every other key, so they only inherit the latter. */
typedef enum {
    KEYMAP_SCOPE_APP,
    KEYMAP_SCOPE_CONVERSATION,
    KEYMAP_SCOPE_EDITOR,
    KEYMAP_SCOPE_HISTORY,
    KEYMAP_SCOPE_SELECTION,
    KEYMAP_SCOPE_CHANNELS,
    KEYMAP_SCOPE_DM_LIST,
    KEYMAP_SCOPE_SERVER_TABS,
    KEYMAP_SCOPE_MEMBERS,
    KEYMAP_SCOPE_MENU,
    KEYMAP_SCOPE_REACTIONS,
    KEYMAP_SCOPE_EMOJIS,
    KEYMAP_SCOPE_MENTIONS,
    KEYMAP_SCOPE_NOTIFICATIONS,
    KEYMAP_SCOPE_SEARCH,
    KEYMAP_SCOPE_MEDIA,
    KEYMAP_SCOPE_PLAYER,
    KEYMAP_SCOPE_AUDIO_PLAYER,
    KEYMAP_SCOPE_FILES,
    KEYMAP_SCOPE_SWITCHER,
    KEYMAP_SCOPE_HELP,
    KEYMAP_SCOPE_SETTINGS,
    KEYMAP_SCOPE_LOGIN,
    KEYMAP_SCOPE_ACCOUNT,
    KEYMAP_SCOPE_PROFILES,
    KEYMAP_SCOPES
} keymap_scope;

/* Display order: application, conversation, lists, selection, members,
 * media, files, sign-in and profiles. */
typedef enum {
    KEYMAP_NONE = -1,
    KEYMAP_QUIT,
    KEYMAP_HELP,
    KEYMAP_HELP_ALL,
    KEYMAP_REFRESH,
    KEYMAP_SETTINGS,
    KEYMAP_SWITCHER,
    KEYMAP_SEARCH,
    KEYMAP_PINNED,
    KEYMAP_MEMBERS,
    KEYMAP_NOTIFICATIONS,
    KEYMAP_ACCOUNT,
    KEYMAP_PROFILES,
    KEYMAP_PLAYER,
    KEYMAP_LOGOUT,
    KEYMAP_RECONNECT,
    KEYMAP_EMOJIS,
    KEYMAP_ATTACH,
    KEYMAP_RETRY_SEND,
    KEYMAP_CANCEL_SEND,
    KEYMAP_SELECT,
    KEYMAP_MENU,
    KEYMAP_CLOSE_DM,
    KEYMAP_ITEM_MENU,
    KEYMAP_REPLY,
    KEYMAP_REACT,
    KEYMAP_REACTORS,
    KEYMAP_MEMBER_SEARCH,
    KEYMAP_COPY_LINK,
    KEYMAP_MEDIA_INFO,
    KEYMAP_MUTE,
    KEYMAP_SEEK_BACK,
    KEYMAP_SEEK_FORWARD,
    KEYMAP_SLOWER,
    KEYMAP_FASTER,
    KEYMAP_LOOP,
    KEYMAP_STOP,
    KEYMAP_MARK_ALL,
    KEYMAP_REGISTER,
    KEYMAP_PROFILE_ADD,
    KEYMAP_PROFILE_EDIT,
    KEYMAP_PROFILE_REMOVE,
    KEYMAP_PROFILE_LIMITS,
    KEYMAP_PROFILE_VERIFY,
    KEYMAP_ACTIONS
} keymap_action;

enum {
    /* Never left without a key: quitting, help and settings stay reachable. */
    KEYMAP_REQUIRED = 1,
    /* Listens while a text field may have focus, so a key needs Ctrl, Alt or
     * must be a function or navigation key; a bare letter would be typed. */
    KEYMAP_TEXT_SAFE = 2,
    /* A bare letter also answers with the other case, as m and M do. */
    KEYMAP_CASELESS = 4
};

/* The keys of every command: defaults, a configuration or a draft. */
typedef struct {
    key_binding keys[KEYMAP_ACTIONS][KEYMAP_BINDINGS];
    unsigned count[KEYMAP_ACTIONS];
} keymap_table;

/* Commands whose keys differ from the defaults, as config.json stores them.
 * Ids this build does not know are kept, so a newer version's choices are not
 * lost by saving here. */
typedef struct {
    char id[KEYMAP_ID_BYTES];
    unsigned count;
    key_binding keys[KEYMAP_BINDINGS];
} keymap_override;
typedef struct {
    keymap_override items[KEYMAP_OVERRIDES];
    size_t count;
} keymap_overrides;

const char *keymap_action_id(keymap_action action);
keymap_action keymap_action_find(const char *id);
/* Translated description of the command. */
const char *keymap_action_label(keymap_action action);
unsigned keymap_action_flags(keymap_action action);
/* Bit (1 << keymap_scope) for each area that hears the command. */
unsigned long keymap_action_scopes(keymap_action action);

void keymap_table_defaults(keymap_table *table);
/* Applies overrides of known commands on top of the defaults. */
void keymap_table_from_overrides(keymap_table *table, const keymap_overrides *overrides);
/* Records what differs from the defaults, keeping unknown ids of `previous`. */
void keymap_table_to_overrides(const keymap_table *table, const keymap_overrides *previous,
                               keymap_overrides *overrides);

/* config.json "shortcuts": {"id": ["ctrl+k", "f3"], ...}; an empty list means
 * no key. Decoding refuses malformed ids or keys and keys a command cannot
 * use; overrides is cleared first. */
bool keymap_overrides_decode(keymap_overrides *overrides, const json_t *object);
json_t *keymap_overrides_encode(const keymap_overrides *overrides);

/* Areas of the Atalhos list, in display order. */
#define KEYMAP_GROUPS 7
/* Translated name of the area a command is listed under; commands of one area
 * are consecutive and share the same pointer. */
const char *keymap_action_group(keymap_action action);
/* Keys that are not commands, listed for reference: navigation, confirmation
 * and text editing. Both strings are translated. */
int keymap_fixed_count(void);
void keymap_fixed(int index, const char **keys, const char **function);

typedef enum {
    KEYMAP_CONFLICT_NONE,
    KEYMAP_CONFLICT_ACTION,
    KEYMAP_CONFLICT_FIXED
} keymap_conflict_kind;
typedef struct {
    keymap_conflict_kind kind;
    keymap_action action; /* The other command, for KEYMAP_CONFLICT_ACTION. */
    const char *label;    /* Translated function that already uses the key. */
} keymap_conflict;
/* Another use of the key where the command also listens: another command of
 * the table, or an editing key of those areas (Ctrl+U, Ctrl+W, Ctrl+N). */
keymap_conflict keymap_find_conflict(const keymap_table *table, keymap_action action,
                                     const key_binding *key);
/* Whether the command listens in the conversation, where push-to-talk takes
 * its key during calls. */
bool keymap_conversation_action(keymap_action action);

/* Whether a key is acceptable for that command; NULL when it is, otherwise the
 * translated reason. */
const char *keymap_binding_error(keymap_action action, const key_binding *binding);
/* Letters shifted without Ctrl or Alt are stored as the shifted character, and
 * shifted letters with Ctrl or Alt as the lowercase letter with Shift. */
void keymap_binding_normalize(key_binding *binding);

/* Keys in effect. Starts with the defaults; safe to call from any thread. */
void keymap_apply(const keymap_table *table);
/* Applies the defaults with the stored overrides on top. */
void keymap_apply_overrides(const keymap_overrides *overrides);
void keymap_current(keymap_table *table);
/* Changes whenever keymap_apply changes the keys in effect. */
uint64_t keymap_revision(void);
/* Keys of the command in effect; returns how many. */
unsigned keymap_keys(keymap_action action, key_binding keys[KEYMAP_BINDINGS]);
/* Whether the key, already normalized, is one of the command's keys. */
bool keymap_matches(keymap_action action, const key_binding *key);
/* Labels of the command's keys in effect joined by " / ", or only the first
 * one; "sem tecla" when it has none. */
bool keymap_label(keymap_action action, bool first_only, char *output, size_t size);

/* Translates msgid and replaces each {k:id} with the labels of that command's
 * keys in effect, or {k1:id} with the first one only. The result is borrowed
 * and stays valid until keymap_shutdown; a later change of keys gives a new
 * string on the next call. Only for the application's own text: remote
 * content never goes through it. In a text with printf conversions, a % in a
 * key label is written as %%, so the result stays a format of the same shape. */
const char *keymap_text(const char *msgid) TRANSLATION_FORMAT(1);
#define K_(text) keymap_text(text)
/* Releases the expanded texts. */
void keymap_shutdown(void);

#endif

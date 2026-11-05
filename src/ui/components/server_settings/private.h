#ifndef PAPO_SERVER_SETTINGS_PRIVATE_H
#define PAPO_SERVER_SETTINGS_PRIVATE_H
#include "ui/components/server_settings.h"
#include <glib.h>
#include <stdio.h>
#include <string.h>
/* The modal is one engine for every tab: a list on the left (for tabs that
 * list entities) and a form on the right. Each tab describes its rows and
 * fields for the current selection; the engine draws them, moves focus and
 * hands activations back to the tab. Remote text is drawn with text_label,
 * which never lets control sequences reach the terminal. */
typedef enum {
    FIELD_INFO,    /* Label and read-only value. */
    FIELD_NOTE,    /* Muted text across the pane. */
    FIELD_HEADING, /* Section title. */
    FIELD_TEXT,
    FIELD_SECRET,
    FIELD_CHECK,
    FIELD_CHOICE, /* ←/→ cycles the value. */
    FIELD_BUTTON
} field_kind;
typedef struct {
    field_kind kind;
    int id, arg;
    char label[128];
    char value[640];
    char key[40];
    text_buffer *text;
    int *first;
    size_t max;
    bool checked, destructive;
    unsigned rgb;
} admin_field;
typedef struct {
    char id[40];
    char label[256], detail[160];
    unsigned rgb;
    bool action; /* "+ Novo …" or "Carregar mais…": drawn in the accent color. */
} admin_row;
/* Rows with these IDs are not entities. */
#define ROW_NEW ""
#define ROW_MORE "more"
typedef struct {
    bool list, search; /* search: typing in the list filters it. */
    const char *empty; /* Shown when the list has no rows. */
    void (*enter)(server_settings *s);
    void (*rows)(server_settings *s, GArray *rows);
    void (*fill)(server_settings *s);
    void (*fields)(server_settings *s, GArray *fields);
    /* direction: 0 Enter/Space/click, -1/+1 ←/→ on a choice. */
    void (*activate)(server_settings *s, const admin_field *field, int direction);
    /* Enter in a text field. */
    void (*submit)(server_settings *s);
    /* A row was chosen in the list (Enter or click). */
    void (*choose)(server_settings *s, const admin_row *row);
} admin_tab_ops;
extern const admin_tab_ops tab_overview, tab_roles, tab_channels, tab_emojis, tab_members,
    tab_audit;

admin_field *admin_field_add(GArray *fields, field_kind kind, int id, const char *label);
void admin_note(GArray *fields, const char *text);
void admin_row_add(GArray *rows, const char *id, const char *label, const char *detail,
                   unsigned rgb, bool action);
void admin_error(server_settings *s, const char *message);
/* UTF-8 of a draft in a buffer owned by the caller. */
bool admin_text(const text_buffer *buffer, char output[TEXT_UTF8_BYTES]);
void admin_set_text(text_buffer *buffer, const char *utf8);
const char *admin_str(const json_t *object, const char *key);
/* A user's display name from the model's cache, or fallback. */
const char *admin_user_name(server_settings *s, const char *id, const char *fallback);
unsigned admin_color(const char *hex);
/* Nickname, or @username's name when there is none. */
const char *admin_user_display(const json_t *user);
/* folded: g_utf8_casefold of the query; "" matches everyone. */
bool admin_user_matches(const json_t *user, const char *folded);
/* Selection changes that discard the drafts (after a save, delete or create). */
void admin_select(server_settings *s, const char *id);
/* Sent a draft: it stays until the server confirms, then refills. */
void admin_sent(server_settings *s);
/* Two presses confirm a destructive button: true on the second. */
bool admin_confirmed(server_settings *s, const admin_field *field);
/* Roles of the catalog in server order, borrowed. */
size_t admin_role_list(server_settings *s, const json_t **roles, size_t capacity);
#endif

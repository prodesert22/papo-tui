#ifndef PAPO_SETTINGS_H
#define PAPO_SETTINGS_H
#include "config/profiles.h"
#include "model/conversation/profile_edit.h"
#include "model/conversation/user_settings.h"
#include "net/voice/client.h"
#include "ui/components/tabs.h"
#include "ui/input/text.h"
#include <ncurses.h>
/* Settings open on the first tab, Perfil. Aparência holds the app modes (this
 * device) and the theme and display of the account on the active server;
 * Notificações holds that account's notification settings; Conta changes the
 * account's password on the active server. */
enum {
    SETTINGS_PROFILE,
    SETTINGS_APPEARANCE,
    SETTINGS_NOTIFICATIONS,
    SETTINGS_AUDIO,
    SETTINGS_ACCESSIBILITY,
    SETTINGS_SHORTCUTS,
    SETTINGS_ACCOUNT,
    SETTINGS_SECTIONS
};
/* Perfil fields, in display order. */
enum {
    PROFILE_FIELD_NICKNAME,
    PROFILE_FIELD_STATUS,
    PROFILE_FIELD_DESCRIPTION,
    PROFILE_FIELD_TYPING
};
#define PROFILE_FIELDS 4
typedef enum { SETTINGS_PICK_NONE, SETTINGS_PICK_AVATAR, SETTINGS_PICK_BANNER } settings_pick;
typedef struct {
    bool open, applied, ptt_capture;
    int section, selection, focus;
    app_mode draft;
    audio_config audio, original_audio;
    voice_client *voice; /* Borrowed; the instance model outlives the modal. */
    int audio_selection, audio_first;
    /* Hit boxes from the last draw. */
    tab_strip tabs;
    WINDOW *window;
    char error[192];
    /* Perfil: the active server's own profile, borrowed while open. */
    history *profile;
    char server[129];
    text_buffer fields[PROFILE_FIELDS];
    int field_first[PROFILE_FIELDS], description_first;
    uint64_t filled_loads, seen_revision;
    bool dirty, requested;
    int profile_item, profile_first, status_choice, loaded_status, banner_button;
    int item_y[7], item_rows[7]; /* Rows from the last draw; -1 when hidden. */
    settings_pick pick;          /* Taken by the view, which opens the file picker. */
    int cursor_y, cursor_x;
    bool cursor;
    /* Conta: the new password and its confirmation, masked and erased on close. */
    auth_client *auth; /* Borrowed, like profile. */
    text_buffer account[2];
    int account_first[2], account_item, account_y[2];
    int profile_bad; /* 1 + the profile field refused, shown in red until edited; 0 none. */
    int account_bad; /* 1 + the password field refused, shown in red until edited; 0 none. */
    bool account_confirm, account_sent;
    unsigned account_seen; /* Outcome shown in the last draw. */
    /* Account settings: the draft, the server copy it was filled from, and
     * whether a save of it is on its way. */
    user_config user, user_base;
    uint64_t user_revision;
    bool user_sent;
    int appearance_first, notify_selection;
} app_settings;
void settings_open(app_settings *settings, const profile_config *config, voice_client *voice,
                   history *profile, auth_client *auth, const char *server);
void settings_close(app_settings *settings);
void settings_draw(app_settings *settings);
void settings_key(app_settings *settings, profile_config *config, int type, uint32_t key);
/* Refreshes the Perfil fields from the server copy; true when a redraw is due. */
bool settings_poll(app_settings *settings);
/* Places the terminal cursor in the focused Perfil or Conta field. */
bool settings_cursor(const app_settings *settings);
/* Inserts pasted text into the focused Perfil or Conta field; false when none is focused. */
bool settings_paste(app_settings *settings, text_paste *paste);
#endif

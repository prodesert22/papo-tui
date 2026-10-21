#ifndef PAPO_HELP_H
#define PAPO_HELP_H
#include "ui/input/shortcuts.h"

typedef uint64_t help_context;
enum {
    HELP_COMMON = 1U,
    HELP_SELECTION = 1U << 1,
    HELP_LOGIN = 1U << 2,
    HELP_PROFILES = 1U << 3,
    HELP_EDITOR = 1U << 4,
    HELP_HISTORY = 1U << 5,
    HELP_CHANNELS = 1U << 6,
    HELP_MEMBERS = 1U << 7,
    HELP_EMOJIS = 1U << 8,
    HELP_NOTIFICATIONS = 1U << 9,
    HELP_SEARCH = 1U << 10,
    HELP_REACTIONS = 1U << 11,
    HELP_FILES = 1U << 12,
    HELP_SETTINGS = 1U << 13,
    HELP_MENU = 1U << 14,
    HELP_MEDIA = 1U << 15,
    HELP_PLAYER = 1U << 16,
    HELP_CONFIRM = 1U << 17,
    HELP_MEMBER_PROFILE = 1U << 18,
    HELP_HEADER = 1U << 19,
    HELP_SERVER_TABS = 1U << 20,
    HELP_FIELDS = 1U << 21,
    HELP_IMAGE = 1U << 22,
    HELP_MEDIA_LIST = 1U << 23,
    HELP_MEDIA_ERROR = 1U << 24,
    HELP_ACCOUNT = 1U << 25,
    HELP_MEMBER_SEARCH = 1U << 26,
    HELP_FIELD_CLEAR = 1U << 27,
    HELP_CONVERSATION = 1U << 28,
    HELP_SETTINGS_BUTTON = 1U << 29,
    HELP_EDITOR_BUTTONS = 1U << 30
};

#define HELP_DM_LIST (UINT64_C(1) << 31)

/* Owns its window and reusable Unicode layout. Borrows no screen/model data.
 * A zero-initialized instance is ready to use; destroy before curses shutdown. */
typedef struct {
    WINDOW *window;
    text_layout *layout;
    text_buffer search;
    bool *matches; /* Owned row visibility, rebuilt only when the query changes. */
    help_context context;
    bool open, global;
    int scroll[2], total, width, rows, switch_x, close_x, focus, search_first;
    const char *area; /* Static application label. */
} help_modal;

bool help_open(help_modal *help, help_context context, const char *area, bool global);
void help_destroy(help_modal *help);
bool help_key(help_modal *help, int type, uint32_t key);
void help_paste(help_modal *help, text_paste *paste);
WINDOW *help_draw(help_modal *help);
#endif

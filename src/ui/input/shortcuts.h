#ifndef PAPO_SHORTCUTS_H
#define PAPO_SHORTCUTS_H
#include "model/text/translation.h"
#include "ui/input/text.h"

/* ASCII control values are shared by legacy terminals and normalized CSI-u.
 * Keep Enter/Tab/Backspace, editing controls and terminal escape codes distinct.
 * The terminal runs without newline translation (nonl), so Enter arrives as CR
 * and Ctrl+J as LF (10): LF is message selection, never Enter, in conversations.
 * Ctrl+A (1) selects all the text of the focused field (text_edit). */
typedef enum {
    SHORTCUT_MENU = 0,
    SHORTCUT_MESSAGES = 10,
    SHORTCUT_PINNED = 2,
    SHORTCUT_NOTIFICATIONS = 4,
    SHORTCUT_EMOJIS = 5,
    SHORTCUT_SEARCH = 6,
    SHORTCUT_HELP = 7,
    SHORTCUT_SWITCHER = 11,
    SHORTCUT_ATTACH = 15,
    SHORTCUT_SETTINGS = 19,
    SHORTCUT_ACCOUNT = 20,
    SHORTCUT_MEMBERS = 21,
    SHORTCUT_PLAYER = 24,
    SHORTCUT_RETRY_SEND = 25,
    SHORTCUT_HELP_ALL = 31
} ui_shortcut;

#define UI_HELP_HINT _("Ctrl+G: ajuda")
static inline bool shortcut_is(int type, uint32_t key, ui_shortcut shortcut)
{
    return (type == OK && key == (uint32_t)shortcut) ||
           (shortcut == SHORTCUT_HELP && type == KEY_CODE_YES && key == KEY_F(1));
}
#endif

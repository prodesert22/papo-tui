#ifndef PAPO_SHORTCUTS_H
#define PAPO_SHORTCUTS_H
#include "config/keymap.h"
#include "model/text/translation.h"
#include "ui/input/text.h"

/* ASCII control values are shared by legacy terminals and normalized CSI-u.
 * Keep Enter/Tab/Backspace, editing controls and terminal escape codes distinct.
 * The terminal runs without newline translation (nonl), so Enter arrives as CR
 * and Ctrl+J as LF (10): LF is message selection by default, never Enter, in
 * conversations. Ctrl+A (1) selects all the text of the focused field
 * (text_edit). Commands are matched against the keys in effect (config/keymap.h). */

#define UI_HELP_HINT K_("{k1:app.help}: ajuda")

/* The key an input event stands for. Control bytes become Ctrl+letter (0 is
 * Ctrl+Space, 31 Ctrl+/), the application's TEXT_* codes their modified keys,
 * and keys registered with text_bindings_configure the configured key. False
 * for events that are no key, such as paste or focus reports. */
bool shortcut_event(int type, uint32_t key, key_binding *binding);
/* Whether the event is one of the command's keys in effect, or the command's
 * own synthetic event (shortcut_code). */
bool shortcut_is(int type, uint32_t key, keymap_action action);
/* A KEY_CODE_YES event that every shortcut_is check of that command accepts,
 * for code that forwards a command to another handler. */
uint32_t shortcut_code(keymap_action action);
/* Registers the keys in effect that the terminal decoder cannot tell apart
 * by itself, such as Alt+letter or Ctrl+Shift+letter, when they changed. */
void shortcut_refresh(void);
#endif

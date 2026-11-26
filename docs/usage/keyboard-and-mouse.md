# Keyboard, mouse and help

## Help dialogs

The footer shows only **Ctrl+G: ajuda** (Open contextual help), alongside
connection, delivery and other operational notices. Click that hint or press
**Ctrl+G** to open **Ajuda do contexto** (Contextual help). The table has
**Combinação de teclas** (Key combination) and **Função** (Action) columns.
Borders and horizontal dividers separate the header, columns and entries.
It lists the controls for the focused editor, history, channel/DM list, member
list or open panel. F1 is also available for contextual help.

Press **Ctrl+/** to open **Ajuda global** (Global help), which lists all
keyboard and mouse controls with their area of use. Both dialogs show the keys
in effect, including the ones changed in
[Ctrl+S → Atalhos](shortcuts.md). The contextual dialog's
**Todos os atalhos** (All shortcuts) button opens the global dialog;
**Ajuda do contexto** returns to the focused-area reference.

Both dialogs open with **Buscar** (Search) focused, below the dialog title; the
contextual dialog keeps the focused area's name above the field. Type to filter
key combinations, functions and area names; matching ignores letter case. In
the contextual dialog the search only looks within the focused area's controls.
Ctrl+F or clicking the field returns to search. Ctrl+U or Ctrl+C clears it, restoring
all entries. Terminal text paste is supported in this field. No matches shows
**Nenhum atalho encontrado.** (No shortcut found). The query stays in place when
switching between the two help dialogs and resets when help is reopened.
In both dialogs Tab/Shift+Tab cycle through search, the table and the buttons.
Enter in search focuses the table; ↑/↓ and PageUp/PageDown scroll results. Home/End move the
cursor in search or jump through results when the table has focus.

Use ↑/↓, PageUp/PageDown, Home/End or the wheel to scroll either table. Tab and
Shift+Tab select its buttons; Enter activates the selected button. Esc or
**Fechar** (Close) returns to the underlying screen with its draft and focus
preserved. Help consumes typing, pasted text and unrelated shortcuts so they
cannot trigger actions underneath it. Ctrl+Q still quits.

Application commands use Ctrl combinations. F2–F10 no longer activate the old
commands. Ctrl+H is reserved for Backspace; Ctrl+I and Ctrl+M remain Tab and
Enter. Ctrl+N still inserts a new line in the conversation editor. Terminals
that do not report Ctrl+/ can reach global help through contextual help's
**Todos os atalhos** button.

## Controls

| Control | Action |
| --- | --- |
| Ctrl+G / F1 | Open contextual help for the focused area or panel |
| Ctrl+/ | Open global help with every keyboard and mouse control |
| Tab / Shift+Tab | Move focus |
| Alt+← / → / ↑ / ↓ in a conversation | Move focus to the area or button in that direction, such as from the channel list to the history, from the history to the message box or from the channels up to the server tabs. The footer shows where each arrow leads, for example **Foco: Histórico  ●  Alt + 🡱 Buscar  ●  Alt + 🡳 Mensagem  ●  Alt + 🡰 Canais  ●  Ctrl+G: ajuda**, when there is room beside the connection and notification notices. On the Linux console, use Tab / Shift+Tab instead; see [The Linux console (TTY)](linux-console.md) |
| Esc | Close a modal, go back or cancel a connection |
| Ctrl+Q | Quit |
| Ctrl+C | Clear the field; quit when outside a field |
| ↑ / ↓ or mouse wheel over history | Scroll the conversation |
| Drag with the mouse over history | Select text and copy it when the button is released |
| PageUp / PageDown in history | Scroll by a page; nearby pages load automatically |
| PageUp / PageDown in the editor | Scroll the conversation one line per press; Shift+PageUp/PageDown scroll a page where the terminal reports the modified key |
| Home / End in history | Go to the start of loaded history / recent messages |
| Ctrl+R | Refresh or retry a request/connection |
| F5 | Reconnect now when the connection drops |
| Ctrl+Y | Retry the last failed send |
| Ctrl+Z | Cancel the newest send in progress or remove it from the queue |
| Ctrl+T | Open the account screen |
| Ctrl+X | Go back to the message of the audio playing in the background and reopen its player |
| Ctrl+S | Open application settings |
| Ctrl+L in a conversation | Sign out of the current account |
| Ctrl+U / 👤 beside 📌/🖈 | Open or close the server member drawer |
| Ctrl+D | Open the notification center and preferences |
| Ctrl+F | Search messages on the current server |
| Ctrl+K in a conversation | [Jump to a conversation](jump-to-conversation.md) of any connected server by name |
| Ctrl+B outside the media player | Open pinned messages in the current conversation |
| Ctrl+E in the editor | Choose an emoji |
| Ctrl+O or Anexar | Choose files to attach |
| Ctrl+V in a conversation | Paste copied files, an image, or text from the local clipboard |
| Shift+Enter or Ctrl+O on a voice channel in the channel list | Open its message history |
| Ctrl+J in a conversation | Enter or leave message navigation; it never sends |
| ↑ / ↓ during message navigation | Select the previous / next message |
| ← / → during message navigation | Select media within the message |
| Enter during message navigation | Open selected media |
| Ctrl+Space or `m` during message navigation | Open or close the selected message's context menu |
| `r` or `R` during message navigation | Reply to the selected message and focus the editor |
| `e` during message navigation | Open the reaction picker |
| Shift+E during message navigation | Show who reacted |
| Right-click a message | Open its context menu directly |
| Click **[ ↓ ]** on an attachment card | Choose where to save that attachment |
| Ctrl+Space, m / M in the channel list, or right-click a channel | Open channel mute and notification settings |
| ↑ in the focused, empty editor | Edit your newest message in loaded history |
| Enter while editing a message | Save the edit |
| Esc while editing a message | Cancel editing and restore the send draft |
| Shift+Enter in the editor | Insert a new line |
| Ctrl+A in a text field | Select all its text. In GNU screen, whose prefix is Ctrl+A, press Ctrl+A and then `a` |
| Ctrl+Backspace / Ctrl+W | Delete the word before the cursor |

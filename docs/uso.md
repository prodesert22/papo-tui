# Using Papo TUI

## Run the application

After [installing](instalacao.md), open a UTF-8 terminal with at least
44 columns and 22 rows and run:

```sh
papo-tui
```

For the Windows ZIP, open PowerShell in the extracted folder and run:

```powershell
.\papo-tui.exe
```

For Flatpak, run `flatpak run io.github.prodesert22.PapoTUI`. To run a local
Linux build from the project folder, use `./build/papo-tui`.

The application attempts to restore the last session when the system credential
store is available.

Use `papo-tui --help` to list command-line options and `papo-tui --version`
to check the installed version.

## Connect and chat

Enter the full instance URL, such as `https://chat.example.com`, your username
and your password. Enter a server password only for private instances.
Select **Entrar** (Sign in). To create an account, select **Cadastro** (Register)
and then sign in.

**Selecionar perfil** (Select profile) lets you save server names and URLs.
To open the profile manager directly, run `papo-tui --profiles`.
Passwords are not stored in the configuration.

Choose a channel with the arrow keys and Enter, or with the mouse. Type in the
editor and press **Enter** to send; **Shift+Enter** inserts a new line. **Ctrl+N**
also inserts a new line. Pasting text does not send the message automatically.
Switching conversations preserves drafts during the current run.

Scroll with the arrow keys, PageUp/PageDown or the mouse wheel over the history.
The next page loads as you approach either edge. A loading indicator appears
if you reach the edge before the messages arrive.

Closing the application discards drafts. Signing out also disables session
recovery. If a send remains unconfirmed, check the history before resending:
the message may already have reached the server.

Voice calls are not available yet.

## Keyboard and mouse

| Control | Action |
| --- | --- |
| Tab / Shift+Tab | Move focus |
| Esc | Close a modal, go back or cancel a connection |
| Ctrl+Q | Quit |
| Ctrl+C | Clear the field; quit when outside a field |
| ↑ / ↓ or mouse wheel over history | Scroll the conversation |
| PageUp / PageDown in history | Scroll by a page; nearby pages load automatically |
| Home / End in history | Go to the start of loaded history / recent messages |
| Ctrl+R | Refresh or retry a request/connection |
| Ctrl+T | Retry the last failed send |
| F6 | Open the account screen |
| F2 | Open application settings |
| Ctrl+L in a conversation | Sign out of the current account |
| F10 | Start a DM |
| F9 | Open the notification center and preferences |
| F4 in the editor | Choose an emoji |
| F7 in a conversation | Enter or leave message navigation |
| ↑ / ↓ during message navigation | Select the previous / next message |
| ← / → during message navigation | Select media within the message |
| Enter during message navigation | Open selected media |
| `m` during message navigation | Open or close the selected message's context menu |
| `r` or `R` during message navigation | Reply to the selected message and focus the editor |
| `e` during message navigation | Open the reaction picker |
| Shift+E during message navigation | Show who reacted |
| Right-click a message | Open its context menu directly |
| ↑ in the focused, empty editor | Edit your newest message in loaded history |
| Enter while editing a message | Save the edit |
| Esc while editing a message | Cancel editing and restore the send draft |
| Shift+Enter in the editor | Insert a new line |

Type `@` in the editor to choose a person to mention; confirm with Tab or Enter.
After closing the chooser, Enter sends the message.

Press F9 to open notifications. Type in **Buscar** (Search) to match message text,
displayed mentions, authors or conversation names. Click the filter on the next
row, or use ←/→ from the results, to choose **Todos** (All), **Canais** (Channels)
or **DMs**. Changing the category keeps you in the notification list. Press Enter
on Canais or DMs to open a selector with its own **Nome** (Name)
field. Type a channel name or the DM recipient's name/@username; use ↑/↓ to
navigate matches and Enter or a mouse click to apply that conversation.
Use ←/→ in the selector's results or click its **Canais**/**DMs** buttons to
switch categories.
Tab reaches **Todos os canais** (All channels) or **Todas as DMs** (All DMs)
when you want the whole category. Esc returns without selecting a conversation.
Click the filter to cycle categories. Click **[Nome]** (Name), immediately beside
the filter on the same row, or press Enter with the filter focused to open its
selector and choose a name. Changing the filter
with a click or arrow never opens the name field automatically.
These selectors use available channel/DM metadata and keep your notification
search text. Click **Data** (Date) to switch between newest and oldest first.
Events without a known date appear after dated results. Tab reaches search,
filter and date controls after **Preferências** (Preferences) and **Mais antigas**
(Older). From the result list, ←/→ changes the conversation filter directly.
Press ↑ on the first result to focus the filter controls, ↑ again for search,
and ↓ to return through the controls to the list. On the filter or date control,
←/→ changes its value; Tab switches between these controls. In search, ←/→ moves
the text cursor. Use ↑/↓ within the results to select a notification.
Enter opens the selected notification; Esc returns to the list or closes the
panel. Search, filters and
ordering apply to loaded notifications. Use **Mais antigas** to load another page
or Ctrl+R to refresh. Merely searching or filtering does not mark notifications read.

In notification details, click **Abrir conversa** (Open conversation) or press
Enter to open its channel or DM and locate the original message. Older messages
are looked up automatically in that conversation. A closed DM is reopened when
you choose this action. The target receives a highlighted
border and stays centered in the history viewport as messages, media and reply
previews finish loading, when enough surrounding content is available. Scrolling
or selecting another message releases this centering. Esc leaves message navigation.
A missing message or a failed lookup
shows a notice; Ctrl+R retries a failed lookup.

Red dots beside server and channel names indicate unread messages. The count
shows known unread mentions of you or `@everyone`. In graphics terminals, the
badge is centered vertically in the row, with a white count inside the red
circle; larger counts use a wider rounded badge. Text channel indicators use
circled digits or parentheses. A `+` on the server badge means more notification
pages are available.
Server badges sit inside the tab delimiters, separated from the name by one
space, for example `[Server ❶]`. With text rendering (Chafa, Light or Ultralight),
the badge is a plain red mention counter, such as `[Server 1]`; zero indicates
unread activity without a confirmed mention.
Counts cover loaded previews, which can be truncated, rather than a server-wide
total. Opening a notification clears its mention badge after confirmation.
The server dot also covers notifications with missing mention metadata.
The unread-message dot remains until the conversation is read. Webhook names
come from their message and remain available when later events omit attribution.
If an author is missing from a notification in the active conversation, its
message is looked up to recover the name without moving your reading position.
For other conversations, **Autor ainda não carregado** (Author not loaded yet)
means the notification omitted attribution. Opening the conversation loads the
message and its author. The client does not fetch inactive conversation histories
just to fill author names, because history reads also mark conversations read.

Type `:` to open emoji suggestions, then keep typing to filter Unicode and
server emojis by name or keyword. Use ↑/↓ to choose, Tab or Enter to insert,
and Esc to dismiss. Confirming a suggestion inserts it without sending the
message. Suggestions place your most frequently inserted emojis first.
Counts are saved per account and instance in the user configuration directory,
under `papo-tui/emoji-usage/`; if it cannot be written, ranking lasts for the
current run.

F4 opens the full emoji picker. Each tile shows a 32×32 preview with its name
below. Use ↓ to move from search to the Unicode/Servidor buttons, then ↓ again
to enter the grid. From its first row, ↑ returns to the buttons; ↑ again returns
to search. Use ←/→ to switch emoji sources while the buttons have focus.
Tab and Shift+Tab also move between these areas.

## Messages, editing, replies and reactions

Press **F7** to select a message with a highlighted border. Use ↑/↓ to move
between messages; older or newer pages load as needed. Use ←/→ to select
media within the message, then Enter to open it. The border shows the selected
media number. Press Esc or F7 to return to your previous focus. Closing the
media viewer returns to the selected message.

Right-click any visible message to open its context menu immediately, or
press **m** while navigating messages to open the menu next to the selected
message. Enter only opens media during message navigation. Press **m** again or
Esc to close the menu. Use the mouse or ↑/↓ and Enter inside the menu to choose:

- **Ver quem reagiu** (See who reacted): show participants grouped by emoji.
  The left column shows rendered emojis and their total counts. Use ↑/↓ to
  highlight an emoji; Enter or → selects it and moves to its participants in
  the right column. Clicking an emoji also selects it. Use ← to return to the
  emoji column and → to move to the people column. Each column has its own
  scroll position: use ↑/↓, PageUp/PageDown, or the mouse wheel over that column.
  More participants load as needed. Esc closes the modal; Ctrl+R retries a
  failed participant request. In message navigation, Shift+E opens this modal
  directly, `e` opens the reaction picker, and `r` focuses the reply editor.
- **Adicionar reação** (Add reaction): choose a Unicode or server emoji without
  changing your draft.
- **Responder** (Reply): prepare a reply in the editor. The strip above the
  editor identifies the original; click **[x]** to cancel the reply.
  The original message stays highlighted while you compose the reply. Press Esc
  once in the editor to cancel the reply while keeping your text and editor focus.
- **Ir para mensagem original** (Go to original message): available on replies.
- **Editar mensagem** (Edit message): available on your own messages. The editor
  shows **Editando mensagem** (Editing message). The message scrolls into view
  and stays highlighted with an **✎ Editando** border until editing ends.
  Change the text and press Enter to save, or Esc / **[x]** to cancel.
  Shift+Enter still inserts a new line.
  Your send draft and reply target return when editing ends.
- **Excluir mensagem** (Delete message): opens a confirmation with **Cancelar**
  (Cancel) selected initially. **Excluir mensagem** uses white text on a red
  background only while focused. Choose it and press Enter, or click that option,
  to delete. Esc cancels. The server permits deletion of your
  own messages or messages covered by your `delete_messages` permission.

With the editor focused and empty, press ↑ to edit your newest own message in
loaded history. This shortcut does not search unloaded pages. With text in the
editor, ↑ keeps its usual cursor behavior. Messages exceeding the editor's
2,048-character limit cannot be loaded for editing.

Editing changes message text and keeps attachments and custom embeds. Empty
edits are refused. Failed edits keep your text for an explicit retry with Enter;
429 responses are never retried automatically. If confirmation is uncertain,
check the refreshed history before retrying. Text typed while an edit is pending
stays in editing mode after confirmation. Esc after submission closes the editor;
an already submitted request may still finish. Changing conversation or server
closes editing; the send draft stays with its conversation. Changing account or
session also closes editing.

Replies show a gray **↰ Author: excerpt** above their content. Click it to
scroll to and highlight the original. Older originals are looked up automatically
in the active conversation, without changing your scroll position. This may
require several history requests. A loading or error label differs from an
unavailable original. Switching conversations cancels pending lookups.

Reactions appear below messages as emoji/count buttons, aligned with the message
text. They wrap onto another line when necessary. Click one to add your reaction;
click it again to remove it. Your reactions are highlighted. Wait for a pending
reaction to finish before making another change. If an operation cannot be
confirmed, the client refreshes the history before you try again.

Reply targets are preserved with each conversation's draft during the current
run, including manual send retries. Confirmed sends clear their reply target.

## Images and video

### Application modes

Press **F2**, or click the gear beside your name in the box below the channel
list, to open **Configurações** (Settings). In **Geral** (General), use ↓ to
reach the modes, ↑/↓ to select one and Space or Enter to mark it. Select
**Salvar e aplicar** (Save and apply) to apply and remember your choice.
**Cancelar** (Cancel), Esc or F2 closes the window without applying changes.
From either bottom button, ↑ returns to the mode list. Use ←/→ on the section
selector to browse the other sections; these are placeholders for future
settings and currently change nothing.

| Mode | Behavior |
| --- | --- |
| Normal | Uses Kitty, Sixel or iTerm2 graphics when available, with Chafa character rendering as a fallback. Animated GIFs and video playback are enabled. |
| Light | Draws images and animated GIFs as colored Chafa characters. Uses the terminal's native Unicode emojis and disables video downloads, covers and playback. |
| Ultralight | Text only, with no media downloads or previews. Attachments appear one per line with their type, name and size. Releases the media helper and removes the space reserved for images and avatars. |

In Light, the media helper exits after three seconds without work. Cached
images remain visible; the helper starts again when needed.
Chafa colors depend on the terminal palette: truecolor terminals preserve RGB
colors, while other terminals use the nearest available colors.

### Open media

Image previews remain visible outside floating panels, including mention/emoji
choosers, message context menus, notifications and reaction dialogs.

Click a media preview to open it, or use F7, select the message and media, then
press Enter. In Normal mode, visible video attachments download automatically in the background
to generate a static cover; they do not play or produce sound in the history.
This consumes bandwidth even before you open the player. The player reuses the
download while it remains cached.

Videos finish downloading before playback starts. Press Esc or click outside
the media window to close it or cancel loading.

| Player control | Action |
| --- | --- |
| Space | Pause or resume |
| ← / → | Seek backward / forward by 5 seconds |
| `,` or `<` / `.` or `>` | Seek backward / forward by 1 second |
| ↑ / ↓ | Increase / decrease the volume |
| `m` | Mute or unmute |
| Esc | Close the player or cancel loading |

On the control bar, click the left button to pause or resume, the timeline to
seek, and the right bar to adjust the volume from 0 to 100%. The volume gradient
is green up to 60%, yellow up to 85% and red above that.
The button shows `[▶]` to resume and `[⏸]` to pause.

Videos scale up or down to fit the player, preserving their aspect ratio without
cropping. Their size adjusts when you resize the terminal.

Click **Info**, or press `i` or F8, to show or hide the media properties.
Use PageUp/PageDown or the mouse wheel over the properties panel to scroll it.

## Troubleshooting

- **Command not found:** check that the application is installed and its folder
  is on PATH. For a portable package, run the executable from the extracted
  folder; see the [installation guide](instalacao.md).
- **Cannot connect:** check the final URL, instance availability and credentials.
  Private servers also require the server password.
- **Untrusted certificate:** install the instance's CA or run
  `CURL_CA_BUNDLE=/path/to/ca.pem papo-tui`. In PowerShell, set
  `$env:CURL_CA_BUNDLE='C:\path\to\ca.pem'` before running the application.
  TLS verification remains enabled.
- **Local HTTP server:** run `papo-tui --allow-local-http`. This option allows
  HTTP only on `localhost`, `127.0.0.1` and `[::1]`.
- **Request limit:** wait for the indicated period and try again.
  Sends are not retried automatically.
- **Images do not appear:** check the application mode in F2. Ultralight shows
  attachment descriptions only. Normal falls back to Chafa when the terminal
  has no supported graphics protocol; Light always uses Chafa.
- **Emoji pictures are missing:** install the Noto Color Emoji font and restart
  the application.
- **Video is still loading:** playback starts after the download finishes.
  Check the connection if it does not progress. Press Esc to cancel; if an
  error appears, press Enter in the media window to retry.
- **Video plays without sound:** press `m` to unmute, raise the volume in the
  player and check the system's audio output. If no audio device is available,
  playback can continue without sound.
- **Missing media helper or module:** reinstall the complete package. For the
  ZIP, keep the DLLs and the `papo-backends` folder alongside the program.
- **Session is not remembered:** unlock the system credential store. Without an
  available store, you must sign in at the next start; cookies are not saved as
  plain text.
- **Small screen or intercepted keys:** enlarge the window and check the
  terminal's reserved shortcuts. Use the mouse if a shortcut does not reach
  the application.
- **Shift+Enter sends instead of adding a line:** use a terminal that supports
  reporting modified Enter, such as Kitty. If the terminal cannot distinguish
  it from Enter, use Ctrl+N to insert a new line.
- **Linux terminal settings were disrupted:** run `reset`, even if the
  characters do not appear as you type.

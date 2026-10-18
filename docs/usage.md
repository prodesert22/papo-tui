# Using Papo TUI

## Run the application

After [installing](installation.md), open a UTF-8 terminal with at least
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

Opening a server automatically opens its first channel in list order, skipping
category separators. Choose another channel with the arrow keys and Enter, or
with the mouse. Categories cannot be selected; arrow keys, Home/End and
PageUp/PageDown skip them. Channel names use color highlighting without a
leading selection marker. Type in the editor and press **Enter** to send;
**Shift+Enter** inserts a new line. **Ctrl+N**
also inserts a new line. Pasting text does not send the message automatically.
Switching conversations preserves drafts during the current run.

Member names use the first valid role color in the order supplied by the server.
Members without a valid role color, and webhook authors, appear in white.
Assigning or removing a role updates the name color when its live event arrives.
Changes to a role's color appear after the role catalog refreshes; Ctrl+R also
requests an update. Terminals with a limited palette approximate the role color.

Scroll with the arrow keys, PageUp/PageDown or the mouse wheel over the history.
The next page loads as you approach either edge. A loading indicator appears
if you reach the edge before the messages arrive.

Closing the application discards drafts. Signing out also disables session
recovery. If a send remains unconfirmed, check the history before resending:
the message may already have reached the server.

To open messages in a voice channel, select it in the channel list and press
**Shift+Enter** or **F4**. Voice calls are not available yet.

## Server members and profiles

The conversation header has **🔍** (Search), **📌** (Pinned messages) and **👤**
(Members). Text rendering uses **🖈** for pinned messages. At the top of the
history, press ↑ to focus these controls, then ←/→ to move between them and
Enter to open one. ↓ returns to the history; on 👤, it enters an open member
drawer. ↑ goes to server tabs. From the channel/DM selector buttons, → after
DMs also reaches Search. Tab and Shift+Tab include all three controls.

Press **F10** or click **👤** beside the pinned-message button to open the member
drawer on the right. It reserves space beside the conversation when the terminal is wide
enough and overlays the right side on narrower screens. Closing or resizing it
preserves your draft and reading position. The conversation footer includes
**F10 membros** (Members), shortened to **F10** when space is limited.

Each member shows an avatar (or their centered first initial), display name and
presence dot: green for online, yellow for away, red for busy and gray for offline. A text label also identifies
the state. **Desconhecido** (Unknown) means presence has not been confirmed;
a saved away/busy preference alone does not establish that someone is online.
Ultralight shows text and an avatar placeholder without loading images.

**Agrupar: Cargo** (Group by role) is the default. Members appear once, under
the first role supplied by the server; this order does not imply a role
hierarchy. People without roles appear under **Sem cargo** (No role). Select
the grouping control to switch to **Agrupar: Status** (Group by status). The
choice is saved locally for future runs across instances.

Press **/** or click **Buscar** (Search) to filter by nickname or @username,
then press Enter to return to the list. Search ignores case and operates on
loaded members; loading and partial-list notices identify incomplete results.
Use Tab/Shift+Tab to move between search, grouping and results. Use ↑/↓,
Home/End or PageUp/PageDown to select people, or use the mouse. Press **Ctrl+R**
to refresh the list and **Esc** or **F10** to close the drawer.

Press **m** on the selected member, or **right-click a member**, to open their
context menu. It sits to the left of the drawer, or above/below the selected row
on narrow screens, keeping the member's name visible. Navigate with ↑/↓ and
Enter, or click an option:

- **Ver perfil** (View profile) opens their profile. Enter on a member also
  opens it directly. The modal shows their banner, avatar, nickname, @username,
  roles, description, presence and status message when available. Avatars and
  banners retain their full image, with padding when their shape differs from
  the available space. Use ↑/↓ or
  the mouse wheel to scroll, Ctrl+R to refresh and Esc to return.
- **Mensagem** (Message) opens or reopens a DM and focuses its editor. The
  server may refuse a DM when either participant has blocked the other.
- **Bloquear / Desbloquear** (Block / Unblock) shows only the applicable action.
  Blocking prevents DMs in both directions and does not hide public-channel
  messages. Unblocking does not automatically reopen a closed DM.
- **Banir / Desbanir** (Ban / Unban) requires the instance owner or
  `manage_server` permission and asks for confirmation. A ban applies to the
  entire instance and revokes the target's sessions. Unbanning requires them
  to sign in again.
- **Gerar link de reset de senha** (Generate password-reset link) has the same
  administrative requirement and asks for confirmation. The result shows a
  single-use link and its expiration. Choose **Copiar** (Copy) or press Enter
  to copy it. A new link invalidates previous links for that account. Closing
  the modal discards the displayed link.

Your own entry offers only the profile. Administrative actions follow confirmed
permissions and the server's response. Menus reuse your account's existing
permission state; opening them does not request permissions again. Block state
loads when the drawer opens, and ban state comes from the member list. If a menu
opens before this finishes, it shows a loading placeholder until its options are
ready. Ctrl+R refreshes state; failures offer an explicit retry. Failed
mutations are never repeated automatically. After an uncertain result, refresh
state before deciding whether to repeat the action. Profile/banner changes made
elsewhere may require reopening the profile or pressing Ctrl+R.

## Direct messages

Choose **DMs** beside **Canais** to see your conversations. ↑/↓ moves the highlighted
recipient; Enter or → opens the selected conversation. The highlight remains
visible when people share the same role color. On wide screens, clicking a DM keeps
focus in the list so the arrows continue selecting people. Typing focuses the
editor. Switching the list or moving its selection does not load other histories.

Press **m** on a selected DM, or right-click its entry, to open a menu with
**Fechar DM** (Close DM). Enter or a click closes that conversation in your list;
Esc or m dismisses the menu. **Ctrl+D** in the DM list also closes the selected
conversation. The menu keeps the recipient's name visible. Closing preserves its remote history; a new message can make it
reappear. Pending sends must finish before you close that DM.

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
| F10 / 👤 beside 📌/🖈 | Open or close the server member drawer |
| F9 | Open the notification center and preferences |
| F3 | Search messages on the current server |
| F8 outside the media player | Open pinned messages in the current conversation |
| F4 in the editor | Choose an emoji |
| F5 or Anexar | Choose files to attach |
| Ctrl+V in a conversation | Paste copied files, an image, or text from the local clipboard |
| Shift+Enter or F4 on a voice channel in the channel list | Open its message history |
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
Enter to open its channel or DM and locate the original message. The modal stays
open and shows **Localizando mensagem…** (Locating message) with an animated
indicator until the destination is ready. Esc cancels navigation and returns to
the details; a read already in flight may finish without navigating to the
canceled target. Older messages are looked up automatically in that conversation.
A closed DM is reopened when
you choose this action. The target receives a highlighted
border and stays centered in the history viewport as messages, media and reply
previews finish loading, when enough surrounding content is available. Scrolling
or selecting another message releases this centering. Esc leaves message navigation.
A missing message or a failed lookup is shown inside the modal. Enter or Ctrl+R
retries explicitly; failures are never retried automatically.

Red dots beside server and channel names indicate unread messages. The count
shows known unread mentions of you or `@everyone`. In graphics terminals, the
badge is centered vertically in the row, with a white count inside the red
circle; larger counts use a wider rounded badge. The number is centered inside
the badge. Text channel indicators use circled digits or parentheses.
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

Animated server emojis (GIF, animated WebP and PNG) play in Normal mode, including
messages, reactions, the editor and emoji selectors. Light shows their first frame
as a static image; attached animations can still play. Ultralight keeps emojis as text.
Animated WebP requires an FFmpeg build with the `webp_anim` decoder.

Messages containing only rendered emojis use larger 48×48 previews and taller
rows, wrapping to fit the conversation. Messages with text and reaction buttons
keep their usual emoji size. Light keeps enlarged server emojis static and uses
native Unicode emojis; Ultralight keeps the text layout.

## Permissions and access

The channel list hides channels you cannot read. Message menus show only the
actions your account can perform; keyboard shortcuts follow the same rules.
In a read-only channel, you can keep a draft, edit or delete your own messages,
view reactions and remove your own reaction. Sending, replying and adding a
reaction require permission to send messages. Administrative roles do not let
you edit another person's messages. Deleting another person's message requires
`delete_messages` in that channel; this does not apply to other people's DMs.

Assigned or removed roles take effect as soon as their WebSocket event arrives,
including while a menu or confirmation is open. Other permission changes are
checked every 30 seconds in the active server tab. Ctrl+R also refreshes
permissions. These checks can take longer while the connection is unavailable
or the server limits requests. Permissions that cannot be confirmed remain
unavailable; the client shows a loading or retry notice.

If you lose access to an open channel, its history closes and your draft stays
with that conversation. Once access returns, select the channel to resume it.
An action still waiting to be sent is canceled if its permission is revoked;
an action already transmitted can still finish. Failed mutations require an
explicit retry. Managing roles, channels and server settings is not available
in this client yet.

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

## Send files and pasted images

Click **📎** to the left of the emoji button, focus it with Tab and press Enter,
or press **F5**. Both buttons have a filled background; the focused button changes
color. While either button has focus, Left or Right switches between them.
The application opens the system file dialog when available. Otherwise, a
terminal file picker opens. Builds with native dialogs disabled open this
terminal picker directly: enter a directory or file in **Caminho** (Path),
then press Enter. In the list, arrows navigate, Enter opens a directory or
marks a file, and Space marks files. Tab moves through path, list, Cancelar
(Cancel), and Anexar (Attach). Select Anexar to add the marked files. Esc
cancels. The picker remembers its last directory during this conversation view.

Pending files appear horizontally as **[filename 🗙]** in a separate block
between the conversation history and the message editor.
Tab follows the visual order: history, pending attachments (when present),
message editor, 📎, and emoji button. Shift+Tab follows the reverse order.
Use Left/Right to select a file, then Enter, or click its 🗙,
to open **Remover anexo?** (Remove attachment?). Cancelar is selected initially.
Choose **Remover** (Remove) to remove it from the draft; the original file is
kept. Long lists scroll horizontally to keep the selected file visible.

Return to the editor and press Enter to send all attachments and optional text
in one message. Selecting or pasting files does not send them immediately.
Attachments stay with each conversation's draft during the current run.
Your roles must allow attaching files and sending messages in that conversation.
This requires `send_attachment` and the conversation's `send_messages` permission.
If access is revoked before sending, the files remain in the draft.
Editing an existing message preserves that send draft; finish or cancel editing
before adding files. Signing out or closing the application discards the drafts.

**Ctrl+V** accepts files copied in a file manager, PNG/JPEG images (including
screenshots), or text. If the clipboard offers several formats, files take
priority over images, then text; only one representation is pasted. Copied
folders and remote file URLs are not attached. Pasting a plain-text path keeps
it as text. On Linux, native clipboard access requires `wl-paste` from
wl-clipboard under Wayland, or `xclip` under X11. Windows uses the native clipboard.
SSH sessions and sandboxed installations without clipboard access can use
📎/F5 for files and the terminal's text paste command. A terminal that
intercepts Ctrl+V must be configured to pass it to the application for native
file/image pasting. In Kitty, add or replace the Papo-specific Ctrl+V mapping
in `kitty.conf` with:

```conf
map --when-focus-on=var:papo_tui=1 ctrl+v send_text all \x16
```

Reload Kitty configuration with Ctrl+Shift+F5 or restart Kitty. This mapping
forwards Ctrl+V while Papo is active, allowing the application to read images
and files directly from the local clipboard.

Preparation copies files privately in the background so later changes to the
original do not change an already prepared attachment. Local limits are **25
files per message**, **512 MiB** of temporary attachment data across drafts and
pending sends, and **64 MiB** per clipboard image. The server can reject uploads
under its own policy. Read failures or insufficient temporary space show a
notice; prepared files and your text remain available. Failed sends require an
explicit retry with Ctrl+T. If the result is uncertain, check history before
confirming a retry, which can duplicate the message.

## Pinned messages and server search

Click **Fixadas** (Pinned) in the conversation header, or press **F8** outside
of the media player, to list that conversation's pinned messages. Type in
**Buscar** (Search) to filter their text, author name or username. Filtering
is local and ignores letter case. Use arrow keys, PageUp/PageDown, Home/End
or the mouse wheel to browse the list. **Ctrl+R** refreshes it.

The message context menu offers **Fixar mensagem** (Pin message) or
**Desafixar mensagem** (Unpin message), once its pin state has loaded.
If it cannot load, keep the menu open and use Ctrl+R to retry.
Pinning and unpinning require `pin_message` and permission to read the
conversation. Viewing the list requires read access without pin permission.
The server remains authoritative; a conversation can have at most 100 pins.
Pinning and unpinning are not repeated automatically after
an error or an uncertain response. Check the refreshed list before retrying.

Click **Buscar** (Search) in the conversation header, or press **F3**, to
search messages on the current instance. Type message content and press
**Enter**, or click **Buscar**, to query the server. Typing alone does not
send requests. The initial scope is the whole server, with newest results
first. Results and text matching follow the server's search behavior.
The whole-server endpoint may omit DMs; select a specific DM to search it.

From the search text field, press Down to reach **Buscar** and **Filtros**.
Left/Right selects between these buttons; Enter activates the selected button.
Down again reaches the results. Up from the first result returns to the buttons,
and Up again returns to the text field. Tab/Shift+Tab also moves between areas.

**Filtros** (Filters) opens a separate screen in the same panel:

- **Autor** (Author), **Conversa** (Conversation) and **Menciona** (Mentions)
  open searchable name selectors. Choose **Qualquer / limpar filtro**
  (Any / clear filter) to remove that restriction.
- **Contém link** (Contains a link) requires a URL in the message content.
- **Data inicial / Data final** (Start / end date) accept `YYYY-MM-DD` and
  include both boundary dates.
- **Anexos** (Attachments) cycles through **Qualquer**, **Com anexos** and
  **Sem anexos** (Any, With attachments and Without attachments).
- **Ordem** (Order) switches between newest and oldest messages first.

Use Tab/Shift+Tab or up/down to reach each filter; the panel scrolls on small
screens. **Aplicar** (Apply) saves the filters and returns to results without
sending a request. **Cancelar** (Cancel) or Esc discards filter edits.
**Limpar filtros** (Clear filters) clears the filter draft. Execute **Buscar**
to apply your text and filters to a new server query. Text may be empty when
at least one filter is set; ordering alone is not a search criterion.

Use **Carregar mais** (Load more) for the next page. Loaded results belong to
the last submitted query; **Alterações pendentes** (Pending changes) means
you must execute Buscar before loading more with edited criteria. Ctrl+R
repeats the current text and filters. A local limit of 1,000 results or 4 MiB
shows a partial-list notice; refine the query when that happens.

In either list, Enter or a click opens message details. **Ir para mensagem**
(Go to message) opens its conversation, locates older messages when needed,
and highlights the target using the same navigation as notifications. The
pinned-message detail also has **Desafixar** (Unpin); the initial selection
is Go to message. Use Left/Right or Tab to select an action, then Enter to
activate it. Up/Down scrolls the preview. Other message actions are available in
the conversation's context menu. Esc returns to the list with its query and
position preserved. Go to message uses the same animated **Localizando mensagem…**
view as notifications, keeping the modal open until the destination is ready.
Esc cancels and returns to details; errors remain in the modal for explicit retry.
Searching or viewing these previews does not open inactive histories or mark
notifications read. Opening the actual conversation has its normal read behavior.

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

The client accepts downloads up to 32 MiB per attached image, 16 MiB per avatar
and 256 MiB per video. Larger files show **Mídia excede o limite de tamanho.**
(Media exceeds the size limit).

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
  folder; see the [installation guide](installation.md).
- **Cannot connect:** check the final URL, instance availability and credentials.
  Private servers also require the server password.
- **Server reports an IP block:** contact the instance administrator. A block
  during private server authorization happens before your account is checked;
  an already open session in another client does not confirm that a new login
  is allowed. Account or IP blocks reported during login are shown separately
  from a refusal to validate an existing session.
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
  it from Enter, use Ctrl+N to insert a new line, or F4 in the channel list to
  open a voice channel's messages.
- **Linux terminal settings were disrupted:** run `reset`, even if the
  characters do not appear as you type.

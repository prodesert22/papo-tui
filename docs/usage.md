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

When the system credential store is available, the application restores the
session of every server you were signed in to. Each server opens as a tab in
the top bar. The server you used last is selected, on the channel or direct
message you had open there; each other tab also returns to its own last
conversation. Without that record, for example on the first start after an
update, the most recently added server is selected. Use **[+ Adicionar]** (Add)
in that bar to sign in to another server.

The sign-in screen keeps the server tabs at the top while another server is
open, for example when the selected server is unreachable at startup or you are
adding a server. Click a tab to open that server, or press ↑ from the first field
(or Shift+Tab), choose a tab with ←/→ and press Enter.

A server that cannot be reached keeps its tab, marked **(offline)**, and its
saved session. Open the tab again to retry: the saved session is used, so you do
not need your password once the server is back.

To remove a server from the list, right-click its tab, or select the tab and
press **m** or Ctrl+Space, to open the tab's menu below it. Choose **Remover
servidor** (Remove server), then confirm **Remover** (Remove). On the server
you are using, the same menu also offers **Configurações do servidor** (Server
settings); see [Server settings](#server-settings). Esc or a click
outside closes the menu. Pressing Delete on a selected tab goes straight to the
confirmation. Removing a server closes the tab and deletes
the session saved in the system credential store; the server does not come back
when you reopen the application. It does not sign you out on the server; use
**Sair da conta** (Sign out) first if you also want to end that session there.

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

Opening a server opens the conversation you last had open there, or its first
channel in list order, skipping category separators. Choose another channel with
the arrow keys and Enter, or
with the mouse. Categories cannot be selected; arrow keys, Home/End and
PageUp/PageDown skip them. Channel names use color highlighting without a
leading selection marker. Type in the editor and press **Enter** to send;
**Shift+Enter** inserts a new line. **Ctrl+N**
also inserts a new line. Pasting text does not send the message automatically.
Switching conversations preserves drafts during the current run.

Returning to a conversation you viewed recently on the same server shows it at
once, at the point where you left it, without loading it again; up to eight
conversations per server are kept this way. Messages that arrived meanwhile
load when you return, if you were reading the latest ones, or when you scroll
down to them. An edit, deletion, reaction or pin while you were away, a dropped
realtime connection or **Ctrl+R** loads the conversation again from the server.

A sent message appears at the end of the history at once, and the editor is
cleared. Sending also brings the history to the newest messages, even if you
had scrolled up to read. Messages from other people and webhooks never move
the history: at the newest messages they appear at the bottom, and while you
read older ones the footer counts them as **novas mensagens** (new messages)
until you scroll down or press End. Until the server confirms your message,
its header shows a tag at the right:

- **enviando** (sending): the server has not confirmed the message yet. The tag
  disappears when it does.
- **falhou** (failed): the server rejected the message. Press **Ctrl+Y** to send
  it again.
- **não confirmado** (not confirmed): the connection failed after the message
  may have reached the server. Check the history before pressing **Ctrl+Y**,
  which asks for confirmation because it can duplicate the message.

Messages that are still sending or failed cannot be selected, replied to,
edited or reacted to, and they are not kept after the application closes.

In terminals that support window titles, the title shows **channel | server**
(or **DM com name | server** for a direct message). A count such as **(3)**
shows the total known unread mentions across connected instances, including
`@everyone`; ordinary unread messages do not add to it. The count disappears
when it reaches zero. Switching conversations updates the title, and returning
to login clears the conversation name. Long names are shortened to fit.
A terminal setting that forces a fixed tab title may override this display.

Member names use the first valid role color in the order supplied by the server.
Members without a valid role color, and webhook authors, appear in white.
Assigning or removing a role updates the name color when its live event arrives.
Changes to a role's color appear after the role catalog refreshes; Ctrl+R also
requests an update. Terminals with a limited palette approximate the role color.

Scroll with the arrow keys, PageUp/PageDown or the mouse wheel over the history.
While the editor is focused, PageUp/PageDown scroll one line per press and
Shift+PageUp/PageDown scroll a full page in terminals that report the modified
key; GNOME Terminal keeps Shift+PageUp/PageDown for its own scrollback. The next
page loads as you approach either edge. A loading indicator appears if you reach
the edge before the messages arrive.

To copy text from the history, press the left mouse button on it and drag; the
selected text is highlighted and copied to the clipboard when you release the
button, and **Texto copiado** (Text copied) appears in the footer. Dragging past
the top or bottom of the history scrolls it, so a selection can span messages
that did not fit on the screen. Author names and times in the selected rows are
copied too; avatar initials and images are not, and custom emoji are copied as
their `:name:`. Another click clears the selection. On Linux the text goes
through `wl-copy` or `xclip`, as when copying from the editor, or through the
terminal (OSC 52) when no desktop session is available; Windows uses the native
clipboard.

Closing the application discards drafts. Signing out of a server also stops
restoring that server; the other servers keep their saved sessions. If a send
remains unconfirmed, check the history before resending: the message may
already have reached the server.

To open messages in a voice channel, select it in the channel list and press
**Shift+Enter** or **Ctrl+O**, or choose **Abrir chat** (Open chat) in its
context menu.

## Voice calls and audio settings

Select a voice channel and press **Enter**, or click it, to join its call.
Your current text conversation and draft stay open. You can keep talking while
reading other channels or switching servers. Only one call is active at a time;
joining another first leaves the previous call. **Sair da chamada** (Leave call)
below the channel list disconnects or cancels a connection in progress.
Connection or session loss ends the call; join again after reconnecting.

Connected members appear below their voice channel. Names are white while idle
and green while that person speaks. Your own entry in the member list also turns
green from your local microphone level while you are transmitting, because
servers may leave the publisher out of their speaker ranking. A **🎤** marker at
the right of the name means the member's microphone is open; **🎤×** in red means
the server reports that member muted. Light, Ultralight and text terminals use
**●** and **●×** instead. No marker is shown until the server reports that
member's microphone state, so a freshly listed member may stay without one
until their first state update. Recent servers include membership in their
initial presence snapshot. Older servers may only provide the member list after
you join. A server's speaker ranking can limit how many simultaneous speakers
are highlighted.

The **🔊** and **🎤** buttons beside the gear toggle playback and microphone
independently. Disabled controls show a cross or a muted speaker. Light,
Ultralight and text terminals use **♪** for playback and **●** for microphone.
You can reach the buttons with Tab and activate them with Enter or Space.
The microphone starts enabled; push-to-talk starts disabled.

Open **Ctrl+S → Áudio** (Audio) to choose input/output devices, change microphone
and call volumes, or configure push-to-talk. Use Tab to focus the options,
↑/↓ to choose one and ←/→ or Enter to adjust it. Device selections follow the
audio devices reported by your system. **Padrão do sistema** uses its default
device. **Atualizar dispositivos** refreshes the list. An unavailable explicitly
selected device stops audio rather than silently selecting another microphone.

**Iniciar teste do microfone** starts a local microphone test. The bar moves
from green through yellow to red as input grows louder, with a peak reading in
dB. The test also plays your microphone back locally, so headphones are useful.
Your microphone is not sent to the call during the test. Stop the test with
**Parar teste do microfone**; leaving the Audio tab or closing settings also
stops it. **Salvar** saves settings; **Cancelar** or Esc restores previous values.

When **Pressionar para falar** (Push-to-talk) is enabled, use the configured
key while talking; the default binding is **Ctrl+Y**. To change it, select
**Tecla para falar** and press Enter, then press the desired combination.
Combinations with Ctrl or Alt are accepted, as are function keys, arrows,
Home/End/Page/Insert/Delete keys on their own, Shift+Tab, and the mouse wheel
buttons **4** and **5**. Letters, numbers, space and symbols need Ctrl or Alt
so ordinary typing is never captured; Shift is folded into the resulting
character. A lone Ctrl, Alt or Shift is ignored while the capture waits for the
rest of the combination. Press Esc to cancel the capture. While a call is
active and push-to-talk is enabled, the configured key is reserved for
push-to-talk; the default Ctrl+Y then does not retry a failed send.

In terminals that report key presses and releases, hold the key to talk and
release it to stop. Terminals that do not report releases still send the legacy
control byte for Ctrl+letter combinations: there, each press of Ctrl+Y turns
transmission on or off, and the key repeat while it is held is ignored. In those
terminals Ctrl+number combinations cannot be told apart from typing the digit,
so choose a Ctrl+letter combination, use the **Segure para falar** button, or
bind mouse button 4/5. A wheel click toggles transmission on and off because
terminals do not send a wheel release. Releasing the key where the terminal
reports releases, pressing the key again on a terminal without them, pressing
the wheel again, losing terminal focus or opening a dialog stops transmission.
Muting the microphone overrides push-to-talk. This control works only while the
application has focus.

If negotiation fails, check audio permissions, device availability and your
server's STUN/TURN configuration. **Fluxos desta instância** is an advanced
setting: ask the administrator for `VOICE_AUDIO_SLOTS` (default 8). It applies
when next joining that server. The backend currently does not advertise this
value automatically. For a source build, see [BUILD.md](../BUILD.md) for required
GStreamer components. No camera or screen sharing is provided.

Builds without the GStreamer WebRTC stack have no voice calls: the call buttons
are absent, the **Áudio** panel reports that the build has no WebRTC support,
and opening a voice channel reports that voice is unavailable.

## Channel notifications and muting

Focus the channel list and press **Ctrl+Space**, **m** or **M**, or **right-click a channel**, to
open its context menu. This also works on a channel you are not reading; opening
the menu does not load that channel's messages. Categories have no context menu.
Use ↑/↓ and Enter, or click an option. In a submenu, ← or Esc returns to the
channel menu; m/M closes it. Closing a menu preserves your draft.

- **Abrir chat** (Open chat), first in the menu of a voice channel, opens its
  messages without joining the call.
- **Silenciar canal…** (Mute channel) offers **10 minutos**, **30 minutos**,
  **1 hora**, **4 horas**, **12 horas**, **24 horas**, and **Até eu reativar**
  (until you reactivate it). A muted channel shows **(silenciado)** in the list;
  its menu offers **Reativar canal** (Reactivate channel).
- **Notificações…** (Notifications) offers **Todas as mensagens** (All messages),
  **Apenas menções** (Mentions only), and **Nada** (None). The checked option is
  the preference in effect. Mentions include direct mentions, replies to
  your messages and `@everyone`, following the server's rules. The default is
  mentions only when the server has no saved preference. **Todas as mensagens**
  only adds system notifications for the other messages: the notification
  center (Ctrl+D) keeps listing only mentions and replies.
- **Sincronizar com o servidor** (Sync with the server), last in the
  **Notificações…** submenu, is checked by default: the chosen option is saved
  on the server and follows you to other devices. Uncheck it to keep this
  channel's option only on this device; the current option is kept, later
  choices are not sent to the server, and the setting survives restarts for
  this account and server. Checking it again uses the server's saved option,
  without sending the device option. The submenu stays open after toggling it.
  The server does not announce preference changes, so opening
  **Notificações…** asks it again for the options saved on other devices; the
  checked option updates as soon as the answer arrives. The client also checks
  every five minutes while connected. A device-only option changed in another
  Papo TUI window on the same computer appears within a second.

Muting pauses this client's system notifications and channel/server activity
indicators for that channel. It does not mark messages or notifications read,
change the server's notification preference, or affect another client. Mutes are
saved separately for each account and instance and survive restarting the
application. Timed mutes expire automatically, including while you are offline;
indefinite mutes last until you choose Reactivate channel.

Notification preferences are saved on the server. A failed or uncertain save
shows an error and is never repeated automatically. Refresh the channels with
Ctrl+R before making another choice when the result is uncertain. If the local
mute file is damaged, inaccessible, or the client detects a conflicting change
from another process, it preserves the file and reports that the mute could not
be saved; fix the file or restart the application after resolving the conflict.

## Server members and profiles

The conversation header has **🔍** (Search), **📌** (Pinned messages) and **👤**
(Members). Text rendering uses **🖈** for pinned messages. At the top of the
history, press ↑ to focus these controls, then ←/→ to move between them and
Enter to open one. ↓ returns to the history; on 👤, it enters an open member
drawer. ↑ goes to server tabs. From the channel/DM selector buttons, → after
DMs also reaches Search. Tab and Shift+Tab include all three controls.

Press **Ctrl+U** or click **👤** beside the pinned-message button to open the member
drawer on the right. It reserves space beside the conversation when the terminal is wide
enough and overlays the right side on narrower screens. Closing or resizing it
preserves your draft and reading position. Use contextual help for the member controls; the footer keeps only **Ctrl+G: ajuda**
(Open help) beside operational notices.

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
to refresh the list and **Esc** or **Ctrl+U** to close the drawer.

Press **Ctrl+Space** or **m** on the selected member, or **right-click a member**, to open their
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

### Edit your own profile

Your profile belongs to each server separately: changes apply to the server tab
that is open. Press **Ctrl+S**: settings open on **Perfil** (Profile), the
first tab. The tab shows **Seu perfil em** (Your profile on)
and the server name, then:

- **Apelido** (Nickname), up to 32 characters. Leave it empty to show only your
  @username.
- **Frase de status** (Status phrase), up to 64 characters.
- **Descrição** (Description), up to 512 characters. Shift+Enter starts a new
  line.
- **Frase de digitação** (Typing phrase), up to 64 characters, shown to others
  while you type.
- **Status**: ←/→ or Enter chooses **Online**, **Ausente** (Away) or
  **Ocupado** (Busy).
- **Avatar** and **Banner**: **Alterar…** (Change…) opens the file choice;
  **Remover** (Remove) deletes the banner.

Each text field shows how many characters it uses. Use ↑/↓ to move between the
fields. Enter in a text field, or **Salvar perfil** (Save profile), saves the
text fields and the status together; **Fechar** (Close) or Esc closes the window,
and unsaved text is discarded. The footer of the window shows **Salvando…**
(Saving), then **Perfil salvo** (Profile saved) or the server's reason when it
refuses a change. An image is sent as soon as you choose it.

You can also type these commands in the message box and press Enter. They
change the profile on the current server and are never sent as messages.
Typing `/` at the start of the message box opens **Comandos** (Commands) above
it, listing each command and what it does; keep typing to narrow the list,
use ↑/↓ to choose and Tab or Enter to complete the name. Once a command is
typed, the box above shows its description and **Opções** (Options), and the
message box shows **+1 opção** (+1 option) until you type the option. For
`/status` and `/banner`, the accepted values are listed and narrow as you
type; Tab completes the chosen one. Esc hides the box and keeps your text.
Enter on a bare command name that takes an option only completes it, so
`/nick` alone removes the nickname after a second Enter.

| Command | Effect |
| --- | --- |
| `/nick name` | Changes your nickname; `/nick` alone removes it. |
| `/status online`, `/status ausente`, `/status ocupado` | Changes your status (`away` and `busy` also work). |
| `/bio text` | Changes your description; `/bio` alone clears it. |
| `/avatar` | Opens the file choice for your avatar. |
| `/banner` | Opens the file choice for your banner; `/banner remover` removes it. |

The footer shows the result. A command with a wrong argument, such as
`/status talvez`, shows how to use it and keeps the text in the box. Other text
that starts with `/` is sent as an ordinary message; start it with `//` to send
a message beginning with `/`, for example `//nick` sends `/nick`.

Avatars and banners can be PNG, JPEG, GIF or WEBP files. The server accepts up
to 2 MB, 512 pixels per side for an avatar and 2048 pixels for a banner. A
larger still image is reduced to fit and sent as PNG; a larger animated GIF,
WEBP or PNG is refused, so reduce the file first. Ultralight has no image
processing, so there only files already within the limits are accepted.

A change that the server refuses or does not confirm is never repeated
automatically. When a change is not confirmed (**Alteração não confirmada**),
the profile is loaded again so you can see what the server kept before trying
again.

## Direct messages

Choose **DMs** beside **Canais** to see your conversations. ↑/↓ moves the highlighted
recipient; Enter or → opens the selected conversation. The highlight remains
visible when people share the same role color. On wide screens, clicking a DM keeps
focus in the list so the arrows continue selecting people. Typing focuses the
editor. Switching the list or moving its selection does not load other histories.

Press **m** on a selected DM, or right-click its entry, to open a menu with
**Fechar DM** (Close DM). Enter or a click closes that conversation in your list;
Esc or m dismisses the menu. **Ctrl+W** in the DM list also closes the selected
conversation. The menu keeps the recipient's name visible. Closing preserves its remote history; a new message can make it
reappear. Pending sends must finish before you close that DM.

## Navigation help

The footer shows only **Ctrl+G: ajuda** (Open contextual help), alongside
connection, delivery and other operational notices. Click that hint or press
**Ctrl+G** to open **Ajuda do contexto** (Contextual help). The table has
**Combinação de teclas** (Key combination) and **Função** (Action) columns.
Borders and horizontal dividers separate the header, columns and entries.
It lists the controls for the focused editor, history, channel/DM list, member
list or open panel. F1 is also available for contextual help.

Press **Ctrl+/** to open **Ajuda global** (Global help), which lists all
keyboard and mouse controls with their area of use. The contextual dialog's
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

## Keyboard and mouse

| Control | Action |
| --- | --- |
| Ctrl+G / F1 | Open contextual help for the focused area or panel |
| Ctrl+/ | Open global help with every keyboard and mouse control |
| Tab / Shift+Tab | Move focus |
| Esc | Close a modal, go back or cancel a connection |
| Ctrl+Q | Quit |
| Ctrl+C | Clear the field; quit when outside a field |
| ↑ / ↓ or mouse wheel over history | Scroll the conversation |
| Drag with the mouse over history | Select text and copy it when the button is released |
| PageUp / PageDown in history | Scroll by a page; nearby pages load automatically |
| PageUp / PageDown in the editor | Scroll the conversation one line per press; Shift+PageUp/PageDown scroll a page where the terminal reports the modified key |
| Home / End in history | Go to the start of loaded history / recent messages |
| Ctrl+R | Refresh or retry a request/connection |
| Ctrl+Y | Retry the last failed send |
| Ctrl+T | Open the account screen |
| Ctrl+X | Go back to the message of the audio playing in the background and reopen its player |
| Ctrl+S | Open application settings |
| Ctrl+L in a conversation | Sign out of the current account |
| Ctrl+U / 👤 beside 📌/🖈 | Open or close the server member drawer |
| Ctrl+D | Open the notification center and preferences |
| Ctrl+F | Search messages on the current server |
| Ctrl+B outside the media player | Open pinned messages in the current conversation |
| Ctrl+E in the editor | Choose an emoji |
| Ctrl+O or Anexar | Choose files to attach |
| Ctrl+V in a conversation | Paste copied files, an image, or text from the local clipboard |
| Shift+Enter or Ctrl+O on a voice channel in the channel list | Open its message history |
| Ctrl+K in a conversation | Enter or leave message navigation |
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
| Ctrl+Backspace / Ctrl+W | Delete the word before the cursor |

Type `@` in the editor to choose a person to mention; confirm with Tab or Enter.
After closing the chooser, Enter sends the message.

Messages that reach your notification center are marked with a colored bar at
the left edge of the history: mentions of you, replies to your messages and
`@everyone` sent by people (applications cannot use `@everyone`). The bar does
not depend on the channel's **Notificações…** setting or on system
notifications. Your own messages are never marked.

Press Ctrl+D to open notifications. The notification center lists what the server
keeps for you: mentions, replies to your messages and `@everyone`. Messages from
a channel set to **Todas as mensagens** (All messages) raise only a system
notification and do not appear in the center or in its count, unless they also
mention you. Type in **Buscar** (Search) to match message text,
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
A conversation counts as read when its latest message is visible at the bottom
of the history while the terminal has focus; only then is the read sent to the
server. Opening a conversation or scrolling through older messages does not mark
it read. Notifications for messages up to the point where you read a
conversation, in this client or another one, are marked read with it and leave
the notification count. Delayed copies of already-read messages do not restore the dot.
The unread-message dot remains until the conversation is read. Your own messages
never light it, and deleting the last message you read does not light it again.
Webhook names come from their message and remain available when later events omit attribution.
If an author is missing from a notification in the active conversation, its
message is looked up to recover the name without moving your reading position.
For other conversations, **Autor ainda não carregado** (Author not loaded yet)
means the notification omitted attribution. Opening the conversation loads the
message and its author. The client does not fetch inactive conversation histories
just to fill author names.

Clicking a system notification, or its **Abrir** (Open) action, opens its
channel or direct message and jumps to the message. A notification from another
server opens when you switch to that server's tab. The click also brings the
terminal running Papo TUI to the front:

- In Kitty, and in other terminals that implement Kitty's notification
  protocol, the terminal shows the notification itself and switches to its
  window and tab.
- Otherwise the notification comes from the desktop, and Papo TUI first selects
  its tab or pane where the terminal allows it (tmux, Konsole and WezTerm), then
  asks the window manager to activate the terminal window. This works on KDE
  Plasma, sway, Hyprland and X11 desktops. GNOME on Wayland does not let
  applications activate another window, so switch to the terminal yourself there.
- In other terminals with tabs, the window comes to the front but keeps the tab
  you were using.
- The Flatpak package cannot find the terminal window from its sandbox: there,
  only terminals that show the notification themselves come to the front.

System notification previews show user mentions as **@username**, rather than
the stored `@mention(<@uuid>)` token. They use the username, even when the person
has a nickname. A user whose username is unavailable appears as
**@usuário indisponível** (user unavailable); `@everyone` stays unchanged.

Type `:` to open emoji suggestions, then keep typing to filter Unicode and
server emojis by name or keyword. Use ↑/↓ to choose, Tab or Enter to insert,
and Esc to dismiss. Confirming a suggestion inserts it without sending the
message. Suggestions place your most frequently inserted emojis first.
Counts are saved per account and instance in the user configuration directory,
under `papo-tui/emoji-usage/`; if it cannot be written, ranking lasts for the
current run.

Ctrl+E opens the full emoji picker. Each tile shows a 32×32 preview with its name
below. Use ↓ to move from search to the Unicode/Servidor buttons, then ↓ again
to enter the grid. From its first row, ↑ returns to the buttons; ↑ again returns
to search. Use ←/→ to switch emoji sources while the buttons have focus.
Tab and Shift+Tab also move between these areas.

Animated server emojis (GIF, animated WebP and PNG) play in Normal mode, including
messages, reactions, the editor and emoji selectors. Light shows their first frame
as a static image; attached animations can still play. Ultralight keeps emojis as text.
Animated WebP requires an FFmpeg build with the `webp_anim` decoder.

Messages containing only rendered emojis use larger previews (about 48×48
pixels) and taller rows, wrapping to fit the conversation. Adjacent enlarged
previews sit directly next to each other and fill the terminal cell grid.
Messages with text and reaction buttons keep their usual emoji size. Light keeps
enlarged server emojis static and uses native Unicode emojis; Ultralight keeps
the text layout.

### Find a GIF

In the emoji picker, choose **[GIF]** (right of **[Servidor]**) to search animated
GIFs on Wikimedia Commons, a public service that needs no account or key. The
search starts only when you pick **[GIF]**, never when the picker opens, and it
waits a moment after you stop typing. Leave the search empty to browse GIFs
without a topic. Results show an animated preview and a title; use the arrow
keys or the mouse to choose one and press Enter to put its address in your
message, then send the message as usual. Ctrl+R repeats the search.

Searches go to `commons.wikimedia.org` and the previews come from
`upload.wikimedia.org`, so those services see your search terms and address.
Commons holds openly licensed files, so it has fewer popular reaction GIFs than
commercial sites. Whether the message shows a preview depends on the instance
turning the link into an embed. The **[GIF]** button is hidden while reacting to
a message, and Ultralight lists titles without previews.

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
explicit retry. Roles, channels and the server itself are managed in
[Server settings](#server-settings).

## Server settings

Open the active server's tab menu (right-click its tab, or select it and press
**m** or Ctrl+Space) and choose **Configurações do servidor** (Server
settings). The panel shows one tab per category. Tabs your account cannot use
are hidden:

| Tab | Shown when your account |
|---|---|
| **Visão geral** (Overview) | owns the server or has **Gerenciar servidor** (Manage server) |
| **Cargos** (Roles) | has **Gerenciar cargos** (Manage roles) |
| **Canais** (Channels) | has **Gerenciar canais** (Manage channels) |
| **Emojis** | always |
| **Membros** (Members) | has **Gerenciar cargos** or **Gerenciar servidor** |
| **Auditoria** (Audit log) | owns the server or has **Gerenciar servidor** |

The server owner sees every tab. While the client is still confirming your
permissions, the bottom of the panel shows **Atualizando permissões…**
(Updating permissions) and tabs that depend on them appear once confirmed. If
you lose a permission while the panel is open, its tab closes, unsaved changes
there are discarded and **Permissão removida; a aba foi fechada.** (Permission
removed; the tab was closed) appears.

Use ←/→ on the tab row, or click a tab, to switch categories. Tab and
Shift+Tab move between the tabs, the list on the left, the form on the right
and **Fechar** (Close). In the list, ↑/↓, PageUp/PageDown and Home/End choose an
item and Enter opens it in the form. In the form, ↑/↓ move between fields,
Enter in a text field saves, Space or Enter toggles a box or presses a button,
and ←/→ change an option shown as **< … >**. Buttons that delete or ban ask
for a second press: the first one changes the label to **Confirmar: …**
(Confirm). Esc closes the panel without saving the open form.

- **Visão geral**: rename the server (up to 32 characters), change or remove
  its icon, and make it public or private. **Alterar ícone…** (Change icon)
  opens the image picker; images larger than 512 pixels are reduced and large
  animations are refused. Making a public server private asks for the server
  password people will use to join; on a private server, **Nova senha** (New
  password) is optional and left empty keeps the current one. The password is
  masked and is not stored by the application. **Salvar alterações** (Save
  changes) sends only what you changed. The owner, creation date and counts are
  shown for reference.
- **Cargos**: choose **+ Novo cargo** (New role) or a role. Edit its name, its
  color as `#RRGGBB` and its seven permissions, then press **Criar cargo**
  (Create role) or **Salvar cargo** (Save role). Leaving the color empty sends
  no color. **Excluir cargo** (Delete role) removes the role from the server.
- **Canais**: choose **+ Novo canal** (New channel) to create a text, voice or
  category channel, or choose a channel to rename it, change its topic (up to
  512 characters; categories have none), move it up or down, or delete it.
  Under **Permissões por cargo** (Permissions per role), each box is saved as
  soon as you change it. **Novo vínculo** (New link) and **Vincular cargo**
  (Link role) add a role to the channel, starting with reading and sending
  allowed; **Remover vínculo** (Remove link) takes the role off the channel
  without deleting it.
- **Emojis**: choose an emoji to see its picture, format, date and author on the
  right; Ultralight shows the details without the picture. Choose
  **+ Adicionar emoji** (Add emoji), type a name and press
  **Escolher imagem e enviar…** (Choose image and send) to pick a GIF, JPEG,
  PNG or WEBP file. You can delete emojis you added; deleting other people's
  emojis needs **Gerenciar servidor**. **Carregar mais…** (Load more) at the
  end of the list fetches the next page.
- **Membros**: type while the list is focused to filter by nickname or
  @username; Backspace erases the filter. With **Gerenciar cargos**, the boxes
  under **Cargos** assign or remove each role at once. With **Gerenciar
  servidor**, **Banir** (Ban) ends the member's sessions and blocks new
  sign-ins, **Desbanir** (Unban) allows them again, and **Gerar link de
  redefinição de senha** (Generate password reset link) shows a single-use
  link and its expiry; a new link replaces earlier ones. You cannot ban
  yourself or the owner.
- **Auditoria**: lists recorded events, newest first. Choose an event to see
  who did it, when, the member affected and its details. **Carregar mais…**
  fetches older events. The first row, **▾ Filtros** (Filters), opens the
  filters and shows how many are in use:
  - **Entidade** (Entity): one kind of item, such as **Cargos** or
    **Usuários** (Users).
  - **Ação** (Action): one action name, such as `role.create`. The options are
    the actions seen in the events loaded since the panel opened.
  - **Feito por** (Done by): type part of a nickname or @username and choose
    the person below the field; Enter picks the only match. **Qualquer
    pessoa** (Anyone) removes this filter.
  - **De** (From) and **Até** (Until): dates as DD/MM/YYYY in this computer's
    time zone; both include the whole day.

  **Aplicar filtros** (Apply filters) reloads the log; **Limpar filtros**
  (Clear filters) shows every event again. From an event, **Só a ação …**
  (Only action …) and **Só as ações de …** (Only actions by …) narrow the
  filters in use to that event's action or author. The server cannot filter
  by the member affected.

The result of each change appears at the bottom of the panel. **Aguarde a
alteração anterior terminar** (Wait for the previous change to finish) means
another change is still being sent. A change refused by the server, such as
**Já existe um cargo com esse nome** (A role with that name already exists),
keeps your text so you can correct it. **Alteração não confirmada; confira
antes de repetir** (Change not confirmed; check before repeating) means the
connection ended before the server answered: the panel reloads what the server
kept, and the change is never sent again automatically. The server does not
announce changes made by other administrators to roles, emojis, channel
permissions or the server itself. They appear when you return to that tab or
reopen the panel; roles and channels also refresh about every 30 seconds.

## Messages, editing, replies and reactions

Press **Ctrl+K** to select a message with a highlighted border. Use ↑/↓ to move
between messages; older or newer pages load as needed. Use ←/→ to select
media within the message, then Enter to open it. The border shows the selected
media number. Press Esc or Ctrl+K to return to your previous focus. Closing the
media viewer returns to the selected message.

Right-click any visible message to open its context menu immediately, or
press **m** while navigating messages to open the menu next to the selected
message. Enter only opens media during message navigation. Press **m** again or
Esc to close the menu. Use the mouse or ↑/↓ and Enter inside the menu to choose:

A horizontal line separates four groups of options: reactions, navigation,
content (download, pin and profile) and, last, actions on your own message. A
group with no available option leaves no line.

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
- **Baixar anexo** / **Baixar anexos** (Download attachment / attachments): save
  the message's attachments to disk. It appears only on messages that have
  attachments; see [Download attachments](#download-attachments).
- **Ver perfil** (View profile): it opens the author's profile,
  the same one shown from the member drawer, without opening the drawer. Esc or a
  click outside closes it; with the drawer open, Esc returns to the drawer.
  Messages sent by applications have no profile and do not show this option.
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

Editing changes message text and keeps attachments and custom embeds. If you
erase all the text and press Enter, the **Excluir mensagem?** (Delete message?)
confirmation opens beside the message, as from the menu: **Excluir mensagem**
deletes it and closes editing, while **Cancelar** (Cancel) or Esc returns to the
edit. Where you cannot delete the message, an empty edit is refused. When you
press Enter, the new text appears in the history at
once and the editor returns to your send draft. Until the server confirms the
edit, the message header shows an **editando** (editing) tag at the right; it is
replaced by **editado** (edited) when the server confirms. Only one edit, deletion,
reaction or pin is sent at a time, so a second edit submitted meanwhile stays in
the editor; press Enter again once the first is confirmed.

If the server rejects the edit, or does not confirm it, the message shows the
server's text again and **Edição não salva** (Edit not saved) appears in the
status bar with the reason. When the send editor is empty, your text returns to
the editor in editing mode, so Enter tries again; otherwise, edit the message
again to get it back. Edits are never retried automatically, including after a
429 response. If confirmation is uncertain, check the refreshed history before
retrying. An edit submitted before changing conversation still finishes, and its
result appears in the status bar. Changing conversation or server closes an open
edit; the send draft stays with its conversation. Changing account or session
also closes editing.

Consecutive messages from the same author share one header with the avatar,
name and time. A message sent more than an hour after the previous one starts
a new header, as do edited messages and replies.

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

## Download attachments

A file attached to a message (anything that is not shown as an inline image or
video) appears as a card with a file icon, its name, its size such as
`2.46 KB`, and a **[ ↓ ]** button at the right edge, filled with the highlight color. Images and videos that
cannot be drawn on your terminal (Light mode, or a terminal without graphics)
use the same card. Ultralight mode lists attachments as text, without a button.

To save a file, click **[ ↓ ]** on its card, or open the message's context
menu (right-click, or `m` during message navigation) and choose **Baixar
anexo** (Download attachment). When the message has two attachments the
item reads **Baixar anexos** and saves both, one at a time. With three or more,
**Baixar anexos** (Download attachments) opens a list with each file name and
size: ↑/↓ or a click moves, Space or Enter marks a file, **A** marks or unmarks
all, and Tab moves to **Cancelar** (Cancel) and **Baixar** (Download). Esc
cancels the whole download. Only the marked files are requested, in list order.
The program always asks where to save before it downloads anything:

- With a desktop session the system **Salvar anexo** (Save attachment) dialog
  opens, with the file name already filled in.
- Without one, or in builds with native dialogs disabled, a terminal dialog
  with the same title opens. **Salvar em** (Save to) holds the folder and file
  name: type a path and press Enter, or move through the list with the arrows;
  Enter on a folder opens it and Enter on a file uses its name. Tab moves
  through the path, list, **Cancelar** (Cancel) and **Salvar** (Save). It starts
  in your Downloads folder when there is one. Choosing a file that already
  exists asks for a second **Salvar** before replacing it. Esc cancels.

Closing the dialog cancels the download (**Download cancelado.**) and nothing
is requested from the server. The footer shows the progress while the file
downloads and **Arquivo salvo em** (File saved to) with the path when it ends.
A file is saved only after it downloads completely, and it can be up to 512
MiB. A failed download reports the reason (**Falha ao baixar**) and leaves no
partial file. Switching to another server tab, signing out or quitting stops
any download in progress.

File names come from the sender, so the program keeps only the name part and
replaces control characters and characters that file systems refuse with `_`
before suggesting it.

## Send files and pasted images

Click **📎** to the left of the emoji button, focus it with Tab and press Enter,
or press **Ctrl+O**. Both buttons have a filled background; the focused button changes
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
📎/Ctrl+O for files and the terminal's text paste command. A terminal that
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
notice; prepared files and your text remain available. A message with files
shows **📎** and the number of files while it is sending. Failed sends keep
their files and require an explicit retry with Ctrl+Y. If the result is
uncertain, check history before confirming a retry, which can duplicate the
message.

## Pinned messages and server search

Click **Fixadas** (Pinned) in the conversation header, or press **Ctrl+B** outside
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

Click **Buscar** (Search) in the conversation header, or press **Ctrl+F**, to
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

Press **Ctrl+S**, or click the gear below your name in the box at the bottom of the
channel list, to open **Configurações** (Settings), then press → to reach
**Aparência** (Appearance). Use ↓ to reach the modes, ↑/↓ to select one and
Space or Enter to mark it. Select
**Salvar e aplicar** (Save and apply) to apply and remember your choice.
**Cancelar** (Cancel), Esc or Ctrl+S closes the window without applying changes.
From either bottom button, ↑ returns to the mode list. Use ←/→ on the section
selector to browse the other sections. **Perfil** and **Áudio** have their own
settings (see [Edit your own profile](#edit-your-own-profile) and
[Voice calls and audio settings](#voice-calls-and-audio-settings));
**Acessibilidade** and **Atalhos** are placeholders for future settings and
currently change nothing.

| Mode | Behavior |
| --- | --- |
| Normal | Uses Kitty, Sixel or iTerm2 graphics when available, with Chafa character rendering as a fallback. Animated GIFs, video and audio playback are enabled. |
| Light | Draws images, animated GIFs, video covers and video playback as colored Chafa characters. Plays audio. Uses the terminal's native Unicode emojis. |
| Ultralight | Text only, with no media downloads, previews or playback. Attachments appear one per line with their type (**Áudio** for sound files), name and size. Releases the media helper and removes the space reserved for images and avatars. |

In Light, the media helper exits after three seconds without work; an open
video, even paused, keeps it running. Cached images remain visible; the helper
starts again when needed.

Normal needs Kitty, Sixel or iTerm2 graphics. On a terminal without them, such
as the Linux console (TTY), the application runs in Light, and **Configurações**
lists only Light, already marked, and Ultralight. If you saved Normal, the
preference is kept for terminals that support it; choosing **Salvar e aplicar**
there saves Light.
Chafa colors depend on the terminal palette: truecolor terminals preserve RGB
colors, while other terminals use the nearest available colors.

Animated GIFs follow their frame timings, and videos can play at up to 60 FPS
when processing speed and terminal throughput allow it. Chafa rendering uses
multiple CPU threads when available, including in a TTY. Video decoding uses
the GPU when a compatible codec and driver are available, and falls back to the
CPU automatically. GIFs and character rendering continue to use the CPU.
Kitty-compatible terminals use compressed graphics to reduce terminal traffic.

### Open media

Image previews remain visible outside floating panels, including mention/emoji
choosers, message context menus, notifications and reaction dialogs.

Attached images can be PNG, JPEG, WebP, GIF or SVG. The server reports an SVG
file as XML, plain text or HTML, so an attachment is shown as an SVG image only
when its name ends in `.svg` (or `.svgz` for a compressed one); other text files
remain attachments. SVG images appear in the history at the size the file
declares, and the image viewer draws them again at its own size, so enlarging
them keeps edges sharp. Pictures and styles that an SVG loads from other files
or from the internet are not loaded; the rest of the drawing is shown. A damaged
SVG shows **Imagem inválida, grande demais ou não suportada.** (Invalid, too
large or unsupported image).

Click a media preview to open it, or use Ctrl+K, select the message and media, then
press Enter. In Normal mode, visible video attachments download automatically in the background
to generate a static cover; they do not play or produce sound in the history.
This consumes bandwidth even before you open the player. Opening the player
reuses that same download, whether it is still running or already cached.

The server reports the type of an attachment from the file's own contents, and
that check recognizes only some video containers. A video whose type is reported
as a generic binary file is still recognized by its file name, so `.mp4`, `.mov`,
`.mkv`, `.webm`, `.avi` and other common containers keep their cover and their
player instead of appearing as a plain attachment.

Videos prepared for streaming start playing as soon as the first part arrives,
while the rest of the download continues. The player timeline keeps the played
part in orange, shows the downloaded part ahead of the playhead in white, and
the part still pending in gray. The orange playhead keeps marking the current
position.

Seeking is enabled after the download finishes. To seek a video that started
while it was still downloading, let the transfer finish and reopen the player;
it will then use the local copy.

Some files can still start only after the download finishes: MP4 files whose
index is stored at the end of the file — usually videos that were not prepared
for streaming — cannot be parsed before their last bytes arrive.

Press Esc or click outside the media window to close it or cancel loading.

The client accepts downloads up to 32 MiB per attached image (including SVG), 16 MiB per avatar
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

The volume you choose is kept when the player closes, when you open the next
video and when you start the application again. It is stored in the same
configuration file as the server profiles, next to the application mode.

Videos scale up or down to fit the player, preserving their aspect ratio without
cropping. Their size adjusts when you resize the terminal.

Click **Info**, or press `i` or Ctrl+B, to show or hide the media properties.
Use PageUp/PageDown or the mouse wheel over the properties panel to scroll it.

### Play audio

Audio attachments appear in the history as a player bar: `[▶]`, the file name,
an empty track and the file size, for example
`[▶] meeting.mp3  ────────────  3,2 MB`. Nothing is downloaded until you open
it: click the bar, or use Ctrl+K, select the message and press Enter. While that
audio plays in the background, its bar shows `[⏸]`, the played part of the track
and the time, such as `1:23 / 3:45`; click `[⏸]`/`[▶]` on the bar to pause or
resume without opening the player. Files
the server reports as a generic type are still recognized by their extension,
such as `.mp3`, `.m4a`, `.ogg`, `.opus`, `.flac` and `.wav`.

The player opens in the media window, titled **Áudio · Esc: segundo plano**
(Audio · Esc: background). It shows the cover embedded in the file when there is
one; otherwise it shows the file name, its type and size, and the playback time.
The control bar and the keys are the same as the video player's, plus:

| Audio control | Action |
| --- | --- |
| `[` / `]` or **[−]** / **[+]** | Slow down / speed up, from 0.5× to 2× |
| `l` or **[Repetir: Não]** / **[Repetir: Sim]** | Repeat the audio when it ends (Repeat: off / on) |
| Space at the end | Start the audio again from the beginning; the status line shows **Fim** (End) |
| `x` or **[Parar]** | Stop the audio and close the player (Stop) |
| Esc or a click outside | Close the player and keep listening in the background |

A new audio starts at normal speed and with repeat off. The volume is shared with
the video player.

While audio plays in the background, a mini-player on the line above the footer
shows the file name, `[⏸]` or `[▶]`, the time and `[✕]`. Click `[⏸]`/`[▶]` to
pause or resume, `[✕]` to stop, or the name to reopen the player. Press **Ctrl+X**
from anywhere in the conversation to go back to the audio's channel or direct
message, jump to its message and reopen the player with its controls; playback
continues where it was. Without background audio, Ctrl+X shows **Nenhum áudio em
segundo plano.** (No background audio).

Background audio keeps playing while you move between channels and direct
messages of the same server. It stops when you switch to another server tab,
open the account screen or profiles, sign out or change the application mode.
Opening another audio or a video replaces it, since there is a single player.

### Browse the media of a message

While the viewer is open and the message carries more than one image, video or
audio, you can move through all of them without closing it. On an image, press
← or → for the previous or the next one. On a video or an audio, ← and → seek,
so use **PageUp** and **PageDown** instead; they work on any media, except
while the properties panel is open, where they scroll it. The sequence wraps
around. The title shows the position, for example **2/3 · clip.mp4**.

The viewer keeps two kinds of text apart. The line above the bottom edge shows
the state, such as **Reproduzindo · 0:07 / 0:30** (Playing). The keys are listed
on the bottom edge itself, starting with **PgUp/PgDn outra mídia** (another
media) when the message has more than one, for example **PgUp/PgDn outra mídia
· Espaço pausar · ←→ 5s · ↑↓ volume** (Space pauses, ←→ seek 5 seconds, ↑↓ change
the volume). On a narrow window the list shrinks to the keys that fit, always
keeping whole entries. Only the media of the opened message are shown; media
from other messages never join the sequence, nor do author avatars. Moving to
another media stops the one that was playing. The properties panel stays open
while you browse, and downloads are reused, so switching back to a media already
seen does not download it again.

Link previews from GIF sites (Giphy, Tenor and Klipy) play in place: the short
silent video behind the link loops like an attached GIF, with no key press, and
the **Abrir player** line below it opens the full player. Longer videos, videos
with sound and previews from other sites keep a still cover. Light mode draws the
loop with colored characters and Ultralight shows the link as text.

A message that contains a direct link to a `.gif`, `.apng` or `.webp` file on Giphy, Tenor, Klipy or
Wikimedia (such as a GIF chosen with **[GIF]** in the emoji picker) shows the GIF
playing in the conversation, up to three per message. The picture is loaded from
that site, which sees your address, and the link text stays in the message.

When a message carries an embed with a video, pressing Enter on the embed cover
opens the video player directly. The **Abrir player** entry keeps working as
before and reuses the download already made for the cover. Videos served by the
embed relay also play while they download, under the same limits described
above.

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
- **A Server settings tab is missing:** your account lacks the permission that
  tab needs; ask the server owner. If the panel shows **Atualizando
  permissões…**, wait a moment or press Ctrl+R in the conversation to refresh
  permissions, then reopen the panel.
- **Images do not appear:** check the application mode in Ctrl+S. Ultralight shows
  attachment descriptions only. Normal falls back to Chafa when the terminal
  has no supported graphics protocol; Light always uses Chafa.
- **Emoji pictures are missing:** install the Noto Color Emoji font and restart
  the application.
- **Video is still loading:** the player status line shows the transfer
  progress. Videos prepared for streaming start before the download ends;
  files whose index is at the end start only when it finishes. If progress
  stops, check the connection. Press Esc to cancel; if an error appears,
  press Enter in the media window to retry.
- **Video plays without sound:** press `m` to unmute, raise the volume in the
  player and check the system's audio output. If no audio device is available,
  playback can continue without sound.
- **Audio plays without sound:** the same checks apply; the status line shows
  **Sem dispositivo de áudio** (No audio device) when the system offers no
  output. If the player shows **Áudio inválido ou codec indisponível.** (Invalid
  audio or unavailable codec), the file is damaged or its format is not supported.
- **Ctrl+X does not reopen the audio:** the message may have been deleted or be
  out of reach; the footer shows **Mensagem do áudio não está disponível.**
  (The audio's message is not available). The mini-player keeps working.
- **Missing media helper or module:** reinstall the complete package. For the
  ZIP, keep the DLLs and the `papo-backends` folder alongside the program.
- **Session is not remembered:** unlock the system credential store. Without an
  available store, you must sign in at the next start; cookies are not saved as
  plain text. A second window of the application does not restore or save
  sessions while the first one is open.
- **Small screen or intercepted keys:** enlarge the window and check the
  terminal's reserved shortcuts. Use the mouse if a shortcut does not reach
  the application.
- **Shift+Enter sends instead of adding a line:** use a terminal that supports
  reporting modified Enter, such as Kitty. If the terminal cannot distinguish
  it from Enter, use Ctrl+N to insert a new line, or Ctrl+O in the channel list to
  open a voice channel's messages.
- **Ctrl+Backspace deletes only one character:** some terminals send the same
  code for Backspace and Ctrl+Backspace. Use Ctrl+W to delete the word before
  the cursor, or a terminal that reports the modified key, such as GNOME
  Terminal.
- **Linux terminal settings were disrupted:** run `reset`, even if the
  characters do not appear as you type.
- **The on-screen keyboard does not open on Android:** in Termux, a tap only
  opens the soft keyboard while the terminal is not reporting mouse events.
  The application pauses the mouse reports as soon as the tap that focuses a
  field ends, so the keyboard opens a moment later. If it stays closed, show it
  with the keyboard button in the Termux drawer.

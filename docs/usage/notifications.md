# Notifications

## Which messages notify you, and how

Three places decide about notifications, each answering a different question:

| Where | What it decides | Applies to |
| --- | --- | --- |
| A channel's menu → **Notificações…** | **Which** messages notify you: all, only mentions, or none | That channel; on every device, or only this one when **Sincronizar com o servidor** is off ([details](channel-notifications.md)) |
| **Ctrl+S → Notificações** | **How** you are notified: system notification, message preview, sound, mentions | Every channel of that server, on every device with that account |
| **Ctrl+D → Preferências** | Whether **this computer** shows notifications, previews and plays sounds at all | Every server, only on this computer |

For example, with `#geral` set to **Todas as mensagens** every message there
notifies you. Turning off **Sons** in Ctrl+S → Notificações keeps the system
notification but without a sound, on all your devices. Turning off **Avisos no
desktop** in Ctrl+D → Preferências stops the system notifications on this
computer only; another computer signed in to the same account still shows them.

A system notification, its preview or a sound happens only when all three allow
it.

## Account notifications

The **Notificações** (Notifications) tab in **Ctrl+S** decides how the account
on the active server is notified. Each switch explains itself below the list
while it is selected:

- **Notificações ativadas** (Notifications enabled): system notifications and
  sounds for this server's alerts. The notification center and the unread
  indicators keep working when it is off.
- **Prévia da mensagem** (Message preview): shows the message text in system
  notifications; without it they say **Nova notificação** (New notification).
- **Sons** (Sounds): notification and voice call sounds.
- **Avisar quando mencionarem você** (Notify me when mentioned): alerts for
  mentions, replies to your messages and `@everyone`. When it is off, they
  still reach the notification center, without a system notification or sound.

Select an item with ↑/↓ and press Space or Enter, or click it, then select
**Salvar** (Save). A refused or unconfirmed save shows a message, keeps your
choices in the window and is never repeated automatically; select **Salvar**
again to try once more. A change made on another device appears here within
half a minute.

## This device

Press **Ctrl+D**, then **Preferências** (Preferences), for the switches of this
computer. They apply to every server and are never sent to one, so you can, for
example, silence a work computer without changing your account:

- **Avisos no desktop** (Desktop notifications): show system notifications.
- **Mostrar prévia** (Show preview): include the message text in them.
- **Sons neste aparelho** (Sounds on this device): play notification and call
  sounds.

## Notification center

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

## Open a notification

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

## Notification authors

Webhook names come from their message and remain available when later events omit attribution.
If an author is missing from a notification in the active conversation, its
message is looked up to recover the name without moving your reading position.
For other conversations, **Autor ainda não carregado** (Author not loaded yet)
means the notification omitted attribution. Opening the conversation loads the
message and its author. The client does not fetch inactive conversation histories
just to fill author names.

## Highlighted messages in the history

Messages that reach your notification center are marked with a colored bar at
the left edge of the history: mentions of you, replies to your messages and
`@everyone` sent by people (applications cannot use `@everyone`). The bar does
not depend on the channel's **Notificações…** setting or on system
notifications. Your own messages are never marked.

## System notifications

Clicking a system notification, or its **Abrir** (Open) action, opens its
channel or direct message and jumps to the message. A notification from another
server first switches to that server's tab. The click also brings the
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
- On Windows, the notification comes from an icon that Papo TUI adds to the
  notification area, shown as the generic application icon. Clicking the
  notification, or that icon later, opens the conversation of the latest
  notification and brings the console or Windows Terminal window to the front,
  on the tab you were using. Once you have read that conversation, a click only
  brings the window to the front. If Windows does not allow the switch, the
  terminal's taskbar button flashes instead.

System notification previews show user mentions as **@username**, rather than
the stored `@mention(<@uuid>)` token. They use the username, even when the person
has a nickname. A user whose username is unavailable appears as
**@usuário indisponível** (user unavailable); `@everyone` stays unchanged.

A system notification always says what the message carries. A message with
attachments and no text shows **[Imagem]** (image), **[Vídeo]** (video),
**[Áudio]** (audio), **[Arquivo: name]** (file) or **[2 anexos]** (two
attachments) in place of the text, and after the text when there is both. A
reply starts with **Respondeu a você** (replied to you) when it answers one of
your messages, **Respondeu a** and the author's name when that message is known,
or just **Respondeu** (replied), followed by the text and attachment. Only the
text of the first 512 characters comes with the server's notification, and the
client does not fetch a message just to describe it: if it never received the
message itself, the notification shows only the server's text, or **Nova mensagem**
(new message) when there is none. With previews turned off, every notification
shows **Nova notificação** (new notification).

## Sounds

Papo TUI plays short sounds of its own:

- a new notification, and a different one when the alert is about you;
- joining and leaving a voice call;
- someone else joining or leaving your call's channel.

Alerts that arrive together sound once, with the sound about you first. No
sound plays for the conversation you are reading in a focused terminal, for a
muted conversation, or while the call output is silenced next to ⚙. On Linux
desktops, system notifications ask the desktop not to play its own sound, so
you do not hear two.

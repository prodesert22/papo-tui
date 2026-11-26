# Channel notifications and muting

A channel's **Notificações…** option chooses which of its messages notify you.
How you are notified (sound, preview, mentions) is set for the whole account in
Ctrl+S; see [Which messages notify you, and how](notifications.md#which-messages-notify-you-and-how).

## Channel menu

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
- **Configurar canal…** (Configure channel), last in the menu, opens
  [Server settings](server-settings.md) on **Canais** with this channel already
  chosen and its form focused, to rename it, change its topic, move it, delete
  it or change each role's access. It appears only when your account can
  manage channels.

## Muting

Muting pauses this client's system notifications and channel/server activity
indicators for that channel. It does not mark messages or notifications read,
change the server's notification preference, or affect another client. Mutes are
saved separately for each account and instance and survive restarting the
application. Timed mutes expire automatically, including while you are offline;
indefinite mutes last until you choose Reactivate channel.

## When a preference is not saved

Notification preferences are saved on the server. A failed or uncertain save
shows an error and is never repeated automatically. Refresh the channels with
Ctrl+R before making another choice when the result is uncertain. If the local
mute file is damaged, inaccessible, or the client detects a conflicting change
from another process, it preserves the file and reports that the mute could not
be saved; fix the file or restart the application after resolving the conflict.

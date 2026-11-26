# Conversations

## Open a conversation and write

Opening a server opens the conversation you last had open there, or its first
channel in list order, skipping category separators. Choose another channel with
the arrow keys and Enter, or
with the mouse. Categories cannot be selected; arrow keys, Home/End and
PageUp/PageDown skip them. Channel names use color highlighting without a
leading selection marker. Type in the editor and press **Enter** to send;
**Shift+Enter** inserts a new line. **Ctrl+N**
also inserts a new line. Pasting text does not send the message automatically.
Only Enter in the message box sends: with the focus on the history, the channel
list or a button, Enter acts there and never sends. **Ctrl+A** selects all
the text in the box, so typing or pasting replaces it.
Switching conversations preserves drafts, and they return the next time you
open the application; see [Drafts](#drafts).

Returning to a conversation you viewed recently on the same server shows it at
once, at the point where you left it, without loading it again; up to eight
conversations per server are kept this way. Messages that arrived meanwhile
load when you return, if you were reading the latest ones, or when you scroll
down to them. An edit, deletion, reaction or pin while you were away, a dropped
realtime connection or **Ctrl+R** loads the conversation again from the server.

## Send a message

A sent message appears at the end of the history at once, and the editor is
cleared. Sending also brings the history to the newest messages, even if you
had scrolled up to read. Messages from other people and webhooks never move
the history: at the newest messages they appear at the bottom, and while you
read older ones the footer counts them as **novas mensagens** (new messages)
until you scroll down or press End. Until the server confirms your message,
its header shows a tag at the right:

- **enviando** (sending): the server has not confirmed the message yet. The tag
  disappears when it does.
- **na fila** (queued): an earlier message of the same conversation, such as a
  large attachment, is still being sent. You can keep writing and sending;
  queued messages go out in order, each after the previous one gets the
  server's answer.
- **falhou** (failed): the server rejected the message. Press **Ctrl+Y** to send
  it again.
- **não confirmado** (not confirmed): the connection failed after the message
  may have reached the server. Check the history before pressing **Ctrl+Y**,
  which asks for confirmation because it can duplicate the message.

To take back a message before the server confirms it, click the **[x]** at
the right of its tag. It removes a queued message, cancels the one being sent,
such as a large upload, or discards a failed or unconfirmed one, and only
affects that message.

From the keyboard, press **Ctrl+J** to select messages: the selection starts at
the newest one, and **↓** below the last message reaches the pending ones.
With a pending message selected, **Delete** removes it like its [x], and
**Enter** or **m** opens its option: **Remover da fila** (remove from queue),
**Cancelar envio** (cancel send) or **Descartar mensagem** (discard message).
Right-clicking a pending message opens the same option. **Ctrl+Z**, from the
editor, removes the newest queued message or cancels the one being sent; press
it again for the next one.

A removed or cancelled message disappears from the history; its text does not
return to the editor. If the server may already have received a cancelled
message, the footer says so and the message stays as **não confirmado** until
you discard it or check the history.

Messages that are still sending or failed cannot be replied to, edited or
reacted to, and they are not kept after the application closes.

## Member name colors

Member names use the first valid role color in the order supplied by the server.
Members without a valid role color, and webhook authors, appear in white.
Assigning or removing a role updates the name color when its live event arrives.
Changes to a role's color appear after the role catalog refreshes; Ctrl+R also
requests an update. Terminals with a limited palette approximate the role color.

## Scroll and copy the history

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

## Messages of a voice channel

To open messages in a voice channel, select it in the channel list and press
**Shift+Enter** or **Ctrl+O**, or choose **Abrir chat** (Open chat) in its
context menu.

## Drafts

Text you type but do not send stays with its conversation, together with the
message you are replying to. It is saved about a second after you stop typing
and when you close the application, and comes back the next time you sign in
to the same account on the same server. Attached files are not kept after the
application closes.

Drafts are stored only on this computer, in a private file readable by your
user alone (`drafts.json` in the application's state directory, such as
`~/.local/state/papo-tui/` on Linux and `%LOCALAPPDATA%\papo-tui\state` on
Windows), and never sent anywhere before you press
Enter. Each account on each server has its own drafts. Signing out of an
account removes its drafts from this computer; deleting the text of a draft
removes it as well.

Signing out of a server stops restoring that server and removes its drafts; the
other servers keep their saved sessions. If a send remains unconfirmed, check
the history before resending: the message may already have reached the server.

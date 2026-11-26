# Messages, editing, replies and reactions

## Select a message

Press **Ctrl+J** to select a message with a highlighted border. Use ↑/↓ to move
between messages; older or newer pages load as needed. Use ←/→ to select
media within the message, then Enter to open it. The border shows the selected
media number. Press Esc or Ctrl+J to return to your previous focus. Closing the
media viewer returns to the selected message.

## Message menu

Right-click any visible message to open its context menu immediately, or
press **m** while navigating messages to open the menu next to the selected
message. Enter only opens media during message navigation. Press **m** again or
Esc to close the menu. Use the mouse or ↑/↓ and Enter inside the menu to choose:

A horizontal line separates four groups of options: reactions, navigation,
content (download, pin, profile, open in browser and copy text) and, last, actions on your
own message. A group with no available option leaves no line.

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
  attachments; see [Download attachments](attachments.md#download-attachments).
- **Ver perfil** (View profile): it opens the author's profile,
  the same one shown from the member drawer, without opening the drawer. Esc or a
  click outside closes it; with the drawer open, Esc returns to the drawer.
  Messages sent by applications have no profile and do not show this option.
- **Abrir no navegador** (Open in browser): opens the message's link in your
  default web browser, and **Link aberto no navegador.** (Link opened in the
  browser) appears in the footer. It appears only when the whole text of the
  message is a single link, written from `http://`, `https://` or `www.`, alone
  or between `<` and `>`; a `www.` address opens over `https://`. The link is
  never opened on its own, only when you choose this option. On Linux it is
  handed to `xdg-open`, from xdg-utils; Windows uses the browser registered in
  the system. To open a link from a longer message, use your terminal's own
  way of opening links; see [Open links from the terminal](formatting-and-links.md#open-links-from-the-terminal).
- **Copiar texto** (Copy text): copies the message's text to the clipboard as
  it was written, with its own line breaks, its Markdown markers and mentions
  written as **@name**. It appears only on messages that have text; drag over the
  history instead to copy only part of a message, or several messages.
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

## Edit a message

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

## Message headers

Consecutive messages from the same author share one header with the avatar,
name and time. A message sent more than an hour after the previous one starts
a new header, as do edited messages and replies.

## Replies

Replies show a gray **↰ Author: excerpt** above their content. Click it to
scroll to and highlight the original. Older originals are looked up automatically
in the active conversation, without changing your scroll position. This may
require several history requests. A loading or error label differs from an
unavailable original. Switching conversations cancels pending lookups.

Reply targets are kept with each conversation's draft, including after a
restart and during manual send retries. Confirmed sends clear their reply target.

## Reactions

Reactions appear below messages as emoji/count buttons, aligned with the message
text. They wrap onto another line when necessary. Click one to add your reaction;
click it again to remove it. Your reactions are highlighted. Wait for a pending
reaction to finish before making another change. If an operation cannot be
confirmed, the client refreshes the history before you try again.

# Files and attachments

## Attach files

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
Attachments stay with each conversation's draft while the application runs;
unlike the text, they are not kept after it closes.
Your roles must allow attaching files and sending messages in that conversation.
This requires `send_attachment` and the conversation's `send_messages` permission.
If access is revoked before sending, the files remain in the draft.
Editing an existing message preserves that send draft; finish or cancel editing
before adding files. Signing out or closing the application discards the attached files.

## Paste files and images

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

## Attachment limits

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

## Download attachments

A file attached to a message (anything that is not shown as an inline image or
video) appears as a card with a file icon, its name, its size such as
`2.46 KB`, and a **[ ↓ ]** button at the right edge, filled with the highlight color. Images and videos that
cannot be drawn on your terminal (Light mode, or a terminal without graphics)
use the same card. Ultralight mode lists attachments as text, without a button.

The server checks uploaded images for sensitive content some time after the
message is sent. When it flags one, its card adds **Moderação: sensitive**
(Moderation: sensitive) and the **Moderação** line in the media viewer's
**Propriedades** (Properties) panel changes to it, even while the viewer is
open, without reloading the conversation. A file the server blocks is removed
with its whole message.

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

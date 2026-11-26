# Images, video and audio

## Images

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

## Open media and play video

Click a media preview to open it, or use Ctrl+J, select the message and media, then
press Enter. In Normal mode, visible video attachments download automatically in the background
to generate a static cover; they do not play or produce sound in the history.
This consumes bandwidth even before you open the player. Opening the player
reuses that same download, whether it is still running or already cached.

A cover that opens the player has a play icon in its middle, which tells a video
apart from a picture: a dark disc with a white triangle when the terminal shows
graphics, or a black box with **▶** when the cover is drawn with characters
(Light mode, or Normal mode without terminal graphics). The icon is part of the
cover only; the player itself shows the video without it.

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

## Player controls

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

## Play audio

Audio attachments appear in the history as a player bar: `[▶]`, the file name,
an empty track and the file size, for example
`[▶] meeting.mp3  ────────────  3,2 MB`. Nothing is downloaded until you open
it: click the bar, or use Ctrl+J, select the message and press Enter. While that
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

## Browse the media of a message

While the viewer is open and the message carries more than one image, video or
audio, you can move through all of them without closing it. On an image, press
← or → for the previous or the next one. On a video or an audio, ← and → seek,
so use **PageUp** and **PageDown** instead; they work on any media, except
while the properties panel is open, where they scroll it. The sequence wraps
around. The title shows the position, for example **2/3 · clip.mp4**.

With the mouse, click the **⬅** (previous) and **➡** (next) buttons at the left
and right edges of the viewer, halfway down. Each is a framed box, nine columns
by five rows, so it is easy to hit; clicking anywhere inside it moves to the
other media. They appear when the message has more than one media and the
window is wide enough for them, and they work on videos and audio too. The
Linux console (TTY) does not show them; use the keys there.

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

## GIF and video links

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
opens the video player directly; that cover shows the same play icon as a video
attachment. Covers of videos the player cannot open, listed as **Vídeo externo**
(external video), have no icon. The **Abrir player** entry keeps working as
before and reuses the download already made for the cover. Videos served by the
embed relay also play while they download, under the same limits described
above.

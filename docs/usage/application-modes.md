# Application modes

Press **Ctrl+S**, or click the gear below your name in the box at the bottom of the
channel list, to open **Configurações** (Settings), then press → to reach
**Aparência** (Appearance). Use ↓ to reach the modes, ↑/↓ to select one and
Space or Enter to mark it. Select
**Salvar e aplicar** (Save and apply) to apply and remember your choice.
**Cancelar** (Cancel), Esc or Ctrl+S closes the window without applying changes.
From either bottom button, ↑ returns to the list. The theme and display items
below the modes are your account's; see
[Theme and history display](settings.md#theme-and-history-display).

## Modes

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

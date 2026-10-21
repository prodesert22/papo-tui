# Unreleased

- Videos start playing while they are still downloading, both for attachments
  served by the instance and for videos opened through embed relays. The player
  timeline keeps the played part orange, shows the downloaded part ahead of the
  playhead in white, and the remaining part in gray. The orange playhead keeps
  marking the current position. Seeking becomes available after the download
  finishes; reopen the player to seek a video that started early. MP4 files
  whose index is stored at the end of the file still wait for the complete
  download before playback starts.
- The player keeps the volume you choose. Closing the player no longer resets
  the volume to its default: the next video starts with the level you set, and
  the level is saved in the configuration file so it survives closing the
  application.
- Video attachments keep their cover and their player when the server reports
  a generic file type for them. The server detects an attachment's type from
  the file contents and recognizes only some video containers, so files such
  as `.mp4`, `.mov`, `.mkv`, `.webm` or `.avi` could appear as a plain
  attachment with no preview and no way to open the player. They are now
  recognized by the file name as well.
- Ctrl+Backspace deletes the previous word in GNOME Terminal and other
  terminals that distinguish it from Backspace. Ctrl+W also deletes the
  previous word; Backspace still deletes one character.
- PageUp/PageDown scroll the conversation while the message editor has focus:
  one line per press, like the arrow keys over the history. Shift+PageUp/PageDown
  scroll a full page in terminals that report the modified key; GNOME Terminal
  keeps Shift+PageUp/PageDown for its own scrollback.
- Long messages no longer show an extra blank line after text that exactly
  fills the conversation width or ends with spaces at the line boundary.
  Message text uses the full drawable width instead of reserving a cell that
  only the editor cursor needs, and trailing spaces that overflow a line are
  dropped instead of wrapping alone.
- Emoji-only messages place their enlarged previews directly next to each
  other; the extra blank cell between adjacent emojis was removed.

## Fixes

- Keep image transfers complete when terminal resizing interrupts output, preventing broken previews during a resize.
- Improve video and animated GIF playback cadence, including Chafa/TTY and Light GIFs, while keeping keyboard controls responsive. Reduce Kitty graphics traffic with compressed pixels for smoother playback in larger windows.
- Enable hardware video decoding in packaged builds when compatible drivers are available (VA-API on Linux and D3D11VA on Windows), with automatic CPU fallback.

## Development

- Reorganize CI into isolated native and instrumented jobs, with individual scenario reports and reproducible test dependencies to diagnose failures and reduce validation time.
- Keep static analysis on an explicit Clang 18 toolchain and verify message-navigation state before accessing its actions.

## Channel notifications

- Open a channel menu with m/M or a right-click to mute it for 10 or 30 minutes, 1, 4, 12 or 24 hours, or until reactivated, and choose all messages, mentions only or no notifications. Timed mutes are saved locally per account and instance.
- Clear stale server activity indicators once their conversations have been read, while preserving unread notification records in the notification center.
- Keep disconnection notices readable in narrow terminals when notification and other status messages compete for space.
- Preserve channel history errors during background channel refreshes so the reason for a failed read remains visible and can be retried.

## Navigation and help

- Open contextual help with Ctrl+G or F1, or global help with Ctrl+/. Both dialogs show a scrollable key-combination/action table with borders and row/column dividers, include mouse buttons, work on narrow screens and preserve the underlying panel, draft and focus. Global help includes an instant, case-insensitive search for key combinations, actions and area names.
- Move navigation shortcut listings into help. The footer now shows only the help hint beside operational notices.
- Replace F2–F10 application commands with Ctrl shortcuts: Ctrl+S settings, Ctrl+F search, Ctrl+B pinned messages, Ctrl+U members, Ctrl+D notifications, Ctrl+O attachments or selected-channel history, Ctrl+E emojis, Ctrl+T account, Ctrl+K message selection and Ctrl+Y retry-send/identify-server. Ctrl+Space opens focused context menus; Ctrl+W closes a selected DM. Text editing, newline, copy/paste and refresh controls remain available.

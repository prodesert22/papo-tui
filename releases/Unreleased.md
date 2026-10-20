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

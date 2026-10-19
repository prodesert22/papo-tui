# Unreleased

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

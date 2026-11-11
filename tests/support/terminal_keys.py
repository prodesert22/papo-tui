"""Keys as a terminal sends them to the application.

The application runs without newline translation, so terminals deliver Enter
as CR and Ctrl+J as LF, and the two do different things. Tests keep writing a
bare "\\n" for Enter: outside bracketed paste it is sent as CR. Ctrl+J is
written with CTRL_J, which is sent as LF. Pasted text keeps its newlines.
"""

CTRL_J = ""  # Private-use placeholder; never typed by tests as text.
PASTE_START, PASTE_END = "\x1b[200~", "\x1b[201~"


def encode_keys(value, pasting=False):
    """Returns the bytes to write as text and whether a paste is still open."""
    out = []
    i = 0
    while i < len(value):
        for marker, state in ((PASTE_START, True), (PASTE_END, False)):
            if value.startswith(marker, i):
                out.append(marker)
                i += len(marker)
                pasting = state
                break
        else:
            character = value[i]
            if character == CTRL_J:
                out.append("\n")
            elif character == "\n" and not pasting:
                out.append("\r")
            else:
                out.append(character)
            i += 1
    return "".join(out), pasting

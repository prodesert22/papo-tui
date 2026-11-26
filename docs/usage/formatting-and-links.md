# Message formatting and links

## Message formatting

Messages in the history show their Markdown formatting instead of the markers
around it. Write them in the editor as usual; the editor itself shows the
markers, and other people see the formatting in their own clients.

| You write | The history shows |
| --- | --- |
| `**bold**` | **bold** |
| `*italic*` or `_italic_` | *italic*, in terminals and fonts that have an italic style |
| `__underlined__` | underlined text |
| `***bold italic***` | bold and italic |
| `___underlined italic___` | underlined and italic; markers also combine when nested, such as `__**underlined bold**__` |
| `~~struck~~` | struck-through text; dimmed text on the Linux console and with text symbols, whose fonts cannot draw the stroke. A single `~` stays as written. It combines with the others, such as `~~**struck bold**~~` |
| `` `code` `` | code on a darker background |
| `[label](https://example.com)` | the underlined label followed by its address in gray, so a label never hides where the link leads |
| `https://example.com` or `www.example.com` | an underlined link, up to the next space: whatever follows the address, such as a `?query` or a `#fragment`, belongs to it, and `*`, `_` and `~` inside it are not formatting. Punctuation right after it, such as a final `.`, `,` or a `)` that the address did not open, stays out |
| `# Title` (up to `######`) | the title in bold |
| `> quote` | the text after a gray **│** bar |
| `- item`, `* item` or `+ item` | the item after a **•** bullet |
| `1. item` or `1) item` | numbered items, counting from the first number |
| `---` or `***` on a line of its own | a short **───** rule |
| a block between two lines of ```` ``` ```` | a box with a darker background across the message width, holding the lines inside exactly as written |

Line breaks and blank lines appear as you typed them. Put a backslash before a
marker to show it as written, such as `\*not italic\*`. Markers without a
partner, such as a lone `**`, also stay as written. HTML tags, tables and
indented text are shown as plain text, and nothing is downloaded for an image
written as `![description](address)`: the history shows its description and
address.

Some lines change meaning, as in other Markdown editors. A line of `---` right
below a text line makes that text a title, and a line right after a quote
continues the quote unless a blank line separates them.

**Copiar texto** (Copy text) in the message menu copies the message as it was
written, markers included. Selecting text in the history with the mouse copies
what the history shows.

## Open links from the terminal

An address written in a message, such as `https://example.com/page#part`, is
also a hyperlink for your terminal. Open it the way your terminal opens links,
often **Ctrl+click** or **Ctrl+Shift+click** (in kitty, Ctrl+Shift+click while a
program uses the mouse), and the whole address opens, also when the link is
too long for one row and continues on the next ones: every row of it leads to
the complete address. Without this, a terminal finds an address only within
one row of the screen and opens a long link cut short, without the end of its
`?query` or `#fragment`.

This needs a terminal with hyperlinks (OSC 8), such as kitty, Konsole, GNOME
Terminal, WezTerm, foot, Ghostty, iTerm2 or Alacritty; others ignore it and
keep finding addresses on their own. It is not used on the Linux console or on
Windows. Only addresses written out in the text become hyperlinks, never the
label of a `[label](address)` link, so what opens is what you read. The part of
a link under a menu, or selected with the mouse, is not a hyperlink until it
shows again. A message that is only a link also has **Abrir no navegador**
(Open in browser) in its context menu.

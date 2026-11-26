# Mentions, emoji and GIFs

## Mention someone

Type `@` in the editor to choose a person to mention; confirm with Tab or Enter.
After closing the chooser, Enter sends the message.

## Emoji suggestions

Type `:` to open emoji suggestions, then keep typing to filter Unicode and
server emojis by name or keyword. Use ↑/↓ to choose, Tab or Enter to insert,
and Esc to dismiss. Confirming a suggestion inserts it without sending the
message. Suggestions place your most frequently inserted emojis first.
Counts are saved per account and instance in the user configuration directory,
under `papo-tui/emoji-usage/`; if it cannot be written, ranking lasts for the
current run.

Unicode emojis have the names they have on Discord, such as `:thumbsup:`,
`:joy:` and `:flag_br:`, and answer to the same aliases: `:+1:`, `:thumbup:`
and `:thumbs_up:` are all 👍, and `:-1:`, `:thumbdown:` and `:thumbs_down:` are
all 👎. Each suggestion shows the emoji's name with its aliases below it.
Emojis that Discord has not named yet have a name in the same style, made from
their Unicode name. The Portuguese and English Unicode names and keywords, such
as `polegar` or `thumbs up`, still find an emoji. A name typed in full is
always the first suggestion, even when another emoji is used more often.
The underscores of a name may be left out while searching: `:pointup` finds
`:point_up:` and `:hearteyes` finds `:heart_eyes:`.

Typing the closing colon of a complete name or alias inserts the emoji at
once: `:+1:` becomes 👍 as you type the second colon. Here the name must be
written as it is, in lower case and with its underscores. To keep the text
instead, press Esc to dismiss the suggestions before typing the colon. Pasted
text is not converted, and a server emoji with the same name keeps its
`:name:` text. Emoticons such as `:)` are not converted.

## Emoji picker

Ctrl+E opens the full emoji picker. Each tile shows a 32×32 preview with its name
below, and the line under the grid shows every name that types the chosen
emoji, such as `:thumbsup: :+1: :thumbup: :thumbs_up:`. The search accepts any
of them, with or without the colons. Use ↓ to move from search to the
Unicode/Servidor buttons, then ↓ again
to enter the grid. From its first row, ↑ returns to the buttons; ↑ again returns
to search. Use ←/→ to switch emoji sources while the buttons have focus.
Tab and Shift+Tab also move between these areas.

## Animated and enlarged emoji

Animated server emojis (GIF, animated WebP and PNG) play in Normal mode, including
messages, reactions, the editor and emoji selectors. Light shows their first frame
as a static image; attached animations can still play. Ultralight keeps emojis as text.
Animated WebP requires an FFmpeg build with the `webp_anim` decoder.

Messages containing only rendered emojis use larger previews (about 48×48
pixels) and taller rows, wrapping to fit the conversation. Adjacent enlarged
previews sit directly next to each other and fill the terminal cell grid.
Messages with text and reaction buttons keep their usual emoji size. Light keeps
enlarged server emojis static and uses native Unicode emojis; Ultralight keeps
the text layout.

## Find a GIF

In the emoji picker, choose **[GIF]** (right of **[Servidor]**) to search animated
GIFs on Wikimedia Commons, a public service that needs no account or key. The
search starts only when you pick **[GIF]**, never when the picker opens, and it
waits a moment after you stop typing. Leave the search empty to browse GIFs
without a topic. Results show an animated preview and a title; use the arrow
keys or the mouse to choose one and press Enter to put its address in your
message, then send the message as usual. Ctrl+R repeats the search.

Searches go to `commons.wikimedia.org` and the previews come from
`upload.wikimedia.org`, so those services see your search terms and address.
Commons holds openly licensed files, so it has fewer popular reaction GIFs than
commercial sites. Whether the message shows a preview depends on the instance
turning the link into an embed. The **[GIF]** button is hidden while reacting to
a message, and Ultralight lists titles without previews.

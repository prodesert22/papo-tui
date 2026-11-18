# Offline emoji catalogue

Vendored Unicode Emoji 17.0 and CLDR 48 input data, distributed under the
Unicode-3.0 license in `LICENSE`:

- `emoji-test.txt`: https://www.unicode.org/Public/17.0.0/emoji/emoji-test.txt
- `en.xml` and `pt.xml`: https://github.com/unicode-org/cldr/tree/release-48/common/annotations
- `en-derived.xml` and `pt-derived.xml`: https://github.com/unicode-org/cldr/tree/release-48/common/annotationsDerived

`discord-names.json` holds the names Discord gives each emoji, the primary
name first and its aliases after it, such as `thumbsup`, `+1`, `thumbup` and
`thumbs_up` for 👍. It is a reduced copy of the `discordEmojiMap.json` that
Emzi0767 extracts from the Discord client and publishes at
https://emzi0767.mzgit.io/discord-emoji/discordEmojiMap.json; the file records
the version it came from. Only the names written between colons are kept:
emoticons such as `:)`, the `:name::skin-tone-N:` spellings, asset addresses
and categories are left out. The names are Discord's; this project found no
license statement for the published file. To update it, download the map and
run `python tools/emoji_catalog.py --import-discord <downloaded file>`.

`python tools/emoji_catalog.py` regenerates `src/model/text/emoji_data.inc`
without network access. `--check` verifies reproducibility. Fully qualified
sequences provide picker entries; composed aliases map to their canonical
entry. Bare text-presentation symbols and plain ASCII digits do not become
pictures. An emoji named by Discord takes that name and its aliases. An emoji
Discord has not named, such as the ones new in Unicode 17, takes its CLDR
English name in lower case with underscores, as in `distorted_face`.

The program uses the system's Noto Color Emoji font through PangoCairo; font
files and graphics from Noto are not bundled here. Newer sequences missing
from this version remain ordinary text rather than changing the sent content.

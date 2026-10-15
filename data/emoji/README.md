# Offline emoji catalogue

Vendored Unicode Emoji 17.0 and CLDR 48 input data, distributed under the
Unicode-3.0 license in `LICENSE`:

- `emoji-test.txt`: https://www.unicode.org/Public/17.0.0/emoji/emoji-test.txt
- `en.xml` and `pt.xml`: https://github.com/unicode-org/cldr/tree/release-48/common/annotations
- `en-derived.xml` and `pt-derived.xml`: https://github.com/unicode-org/cldr/tree/release-48/common/annotationsDerived

`python tools/emoji_catalog.py` regenerates `src/model/emoji_data.inc` without
network access. `--check` verifies reproducibility. Fully qualified sequences
provide picker entries; composed aliases map to their canonical entry. Bare
text-presentation symbols and plain ASCII digits do not become pictures.

The program uses the system's Noto Color Emoji font through PangoCairo; font
files and graphics from Noto are not bundled here. Newer sequences missing
from this version remain ordinary text rather than changing the sent content.

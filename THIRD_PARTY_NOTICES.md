# Third-party components

The MIT license at the repository root covers original code. Each library
retains its own license. The Unicode/CLDR catalog retains the license in
data/emoji/LICENSE.

| Component | Purpose | Upstream license |
| --- | --- | --- |
| ncursesw/formw | Terminal and forms | MIT/X11 |
| libcurl | HTTP, TLS and WebSocket | curl |
| Jansson | JSON | MIT |
| libunibreak | Unicode segmentation | zlib |
| GLib/GIO/GModule | Data structures, tasks, integration and modules | LGPL-2.1-or-later |
| libsixel | Sixel encoding | MIT |
| Chafa | Terminal graphics and character rendering | LGPL-3.0-or-later |
| Pango/Cairo | Text and emojis in images | LGPL-2.1-or-later / LGPL-2.1-or-later OR MPL-1.1 |
| Fontconfig | Font discovery | MIT |
| FFmpeg | Image decoding and video pixel conversion | LGPL-2.1-or-later in distributed configurations |
| libmpv | Video and audio playback | LGPL-2.1-or-later in distributed configurations |
| libplacebo / libass | libmpv dependencies | LGPL-2.1-or-later / ISC |
| libva / libdrm | Linux hardware video decoding | MIT/X11 |
| Noto Emoji | Emoji font | OFL-1.1 |

Packages that bundle libraries must include their exact licenses and publish
the corresponding sources, patches and build options alongside the binaries.
Official recipes disable GPL/nonfree components in FFmpeg and libmpv; this
selection also applies to transitive dependencies. System libraries remain
under the distributor's configuration and license.

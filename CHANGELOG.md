# Changelog

Versions follow `major.minor.patch`. This file summarizes each release;
full details are available in `releases/`.

## Unreleased

Upcoming changes are recorded in [Unreleased](releases/Unreleased.md).

## [0.4.0]

### Added

- File attachments through F5 or the paperclip button, with native file dialogs,
  a terminal picker fallback, removable draft files and attachment-only messages.
- Ctrl+V support for copied local files, clipboard images and text, with private
  background preparation and attachment drafts preserved per conversation.
- F8 pinned-message browsing with scrolling, local author/text search, message
  details and permission-aware pin/unpin actions.
- F3 server message search with author, conversation, mention, link, date,
  attachment and ordering filters, searchable selectors and result pagination.
- Permission-aware channel visibility and message actions, live role membership
  updates, periodic permission refresh and role-colored member names.
- Server member drawer through 👤 or F10, with nickname/@username search,
  saved role/status grouping, avatars, presence and full profile previews.
- Member context menus through m or right-click, with DMs, block/unblock and
  permission-gated ban/unban and password-reset links.
- DM context menus through m or right-click to close a conversation.

### Changed

- Keep notifications, pinned messages and search in their modal with an animated
  locating view until the requested message is ready; allow cancellation and
  explicit retries there.
- Locate older destinations with known message timestamps using bounded cursor
  reads instead of walking all intervening history pages.
- Label F9 consistently as notifications and show discovery shortcuts in the
  footer; use F3 for search and F5 for attachments.
- Use 🔍, 📌 (🖈 in text mode) and 👤 in the conversation header, with arrow-key
  navigation between controls, the history and the member drawer.
- Keep arrow navigation in the DM list after clicking a conversation, with a
  visible selection that preserves role colors. Context menus keep names visible.
- Restrict release runs to pushes changing CMakeLists.txt on main and require a
  version increase before tagging or packaging. Identify PR checks as CI and
  block release-mode builds in other event/branch contexts.

### Fixed

- Center mention counts inside notification badges and remove the extra + suffix.
- Keep replies separate from adjacent messages in history grouping.
- Use ncurses 6.6 across Linux and Windows builds so rapid mouse drags preserve
  text selection and copying.
- Identify server-reported IP blocks during private instance authorization and
  distinguish other access refusals by authentication stage.

See the [full 0.4.0 release notes](releases/v0.4.0.md).

## [0.3.1]

- Run routine CI on pull requests to main, including version commits, and cancel
  superseded runs when the PR receives another commit.
- Publish releases automatically after an increased version is merged from the
  permanent dev branch, reusing validated native package artifacts.
- Keep source archives and SHA256SUMS, and reject conflicting tags or attempts
  to overwrite published releases.
- Use English filenames for the installation and usage guides, updating links
  and installed documentation paths.

See the [full 0.3.1 release notes](releases/v0.3.1.md).

## [0.3.0]

### Added

- F7 message navigation, attachment selection and context menus opened with
  `m` or a right-click, with direct reply/reaction shortcuts.
- Replies with author/excerpt previews, original-message lookup and navigation,
  highlighted targets and reply drafts preserved through manual retries.
- Editing your own messages, including the empty-editor Up shortcut, a dedicated
  edit buffer and attachment/custom-embed preservation; deletion with confirmation.
- Reaction toggles with highlighted own reactions and a two-column participant
  panel with independent scrolling, filtering and pagination.
- Notification search, channel/DM filters, searchable conversation selectors,
  explicit Name controls and precise newest/oldest ordering.
- Animated GIF/APNG/WebP server emojis in Normal mode; larger client emoji/catalog
  loading bounds and 48×48 emoji-only messages with wrapped, taller rows.
- Static video covers for visible attachments in Normal mode, reusing cached
  downloads when opening playback.
- Automatic opening of the first channel, non-selectable category separators
  and channel selection indicated by color without leading markers.
- Ctrl+Backspace word deletion and Shift+Enter access to voice-channel messages,
  retaining F4 and editor newline behavior.

### Improved and fixed

- Fix Windows Unicode input with a private ncurses console compatibility patch,
  preserving accented text, characters that overlap function-key codes and emojis.
- Reuse conversation measurement storage to prevent stack exhaustion when
  opening message history on Windows and other terminals with a small stack.
- Wait for editor text before asserting soft wraps and dragging selections in
  terminal tests, including slower sanitizer builds.
- Notification destinations retain the target across channel changes, closed
  DMs, older-history lookups, delayed media loading and terminal resizing.
- Recover notification authors and webhook names from available message metadata;
  preserve attribution across partial events without fetching inactive histories.
- Distinguish unread activity and mention counts with centered graphical badges,
  red text counters and correct spacing inside server tabs.
- Preserve uncovered history images, avatars and emojis behind context menus,
  mention/emoji selectors, notifications and reaction panels across renderers.
- Retire stale Kitty reaction placements without removing shared history images;
  keep navigation icons through partial redraws.
- Keep attached animations independent of static Light-mode emoji variants, and
  preserve reading anchors when enlarged emojis wrap, scroll or resize.
- Raise client download limits to 32 MiB per attached image, 16 MiB per avatar
  and 256 MiB per video, with a larger cached-file budget.
- Preserve drafts and operation ownership across context changes; distinguish
  lookup failures from missing messages and require explicit mutation retries.
- Expand contract fixtures, synthetic HTTP/WS integration, terminal interaction,
  graphics protocol, display-mode and keyboard encoding coverage.

See the [full 0.3.0 release notes](releases/v0.3.0.md).

## [0.2.0]

- Add Normal, Light and Ultralight application modes in the F2 settings modal.
  Light renders images and animated GIFs with colored Chafa characters and
  disables video; Ultralight shows media as text and skips media downloads.
- Use Chafa when a terminal has no supported graphics protocol, and preserve
  media colors in both direct-color and indexed-color terminals.
- Move media decoding and rendering to an on-demand helper process. The helper
  exits after idle time in Light mode; Ultralight avoids starting it entirely.
- Include GLib subprocess helpers in Windows packages so the media helper can start.
- Consolidate CI builds by native platform and architecture, reuse dependency
  caches and run independent builds concurrently with separate validation logs.
- Document known Windows Unicode input failures and an unresolved sanitizer
  terminal test failure in the release notes.

See the [full 0.2.0 release notes](releases/v0.2.0.md).

## [0.1.2]

- Insert new lines with Shift+Enter in supported terminals; Enter still sends
  messages and Ctrl+N remains available as a fallback.
- Preserve numeric keypad shortcuts and restore the terminal's previous
  keyboard mode on exit.

See the [full 0.1.2 release notes](releases/v0.1.2.md).

## [0.1.1]

- Emoji completion on `:` with filtered Unicode and server suggestions, saved
  usage ranking, labeled 32×32 picker previews and complete keyboard navigation.
- Keep uncovered chat images visible while the emoji picker is open.

See the [full 0.1.1 release notes](releases/v0.1.1.md).

## [0.1.0]

- Registration, sign-in, multiple instances and recovery of the last session
  through the system credential store.
- Channels, DMs and text conversations in voice channels, with real-time messages,
  drafts, presence, typing indicators, notifications and persistent read state.
- History paginated in both directions, preserving the reading position during
  scrolling, focus changes and resizing.
- Mentions, emojis, avatars, image galleries, animations, embeds and video.
- Shared media and avatar cache; fixes for focus, mouse handling, rendering and
  video playback in locales with a decimal comma. Videos scale proportionally
  to fit the player, including when resizing during paused playback.
- Preparation of Linux and Windows packages for x86_64 and ARM64, code
  reorganization, simpler documentation and CI with tests, static analysis and coverage.

See the [full 0.1.0 release notes](releases/v0.1.0.md).

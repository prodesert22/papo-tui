# Changelog

Versions follow `major.minor.patch`. This file summarizes each release;
full details are available in `releases/`.

## Unreleased

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

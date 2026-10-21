# Papo TUI

Terminal chat client written in C. Connect to independent Papo instances and
chat in channels or direct messages, with message editing, deletion, replies,
reactions, pinned messages, server message search, notifications, mentions, emojis
and media in supported terminals.

Press **Ctrl+G** for a table of shortcuts and mouse controls for the focused
area, or **Ctrl+/** for the global reference. The footer keeps only the help
hint alongside operational notices. Global help includes a search field for
keys, actions and areas. F1 also opens contextual help.

Attach files with **Ctrl+O** or **📎**, or paste copied files and images with
**Ctrl+V** when the local clipboard is available.
Available channels and message actions follow your account permissions.
Member names follow their role color, with white as the default.
Open the member drawer with **👤** or **Ctrl+U** to search people, view profiles,
start direct messages and access available member actions.
Use the arrows to select DMs, and **m** or a right-click to close one from its menu.
Press **m/M** in the channel list, or right-click a channel, to mute it or
choose its notification preference.
The terminal window title shows the current conversation and server, plus the
total known unread mentions across connected instances when there are any.

## Install

Packages are available from [Releases](https://github.com/prodesert22/papo-tui/releases).
Choose your operating system and architecture: **x86_64** or **ARM64**.

| System | Formats |
| --- | --- |
| Linux | DEB, RPM, Arch, AppImage and Flatpak |
| Windows | Portable ZIP and installer |

See the [installation guide](docs/installation.md) and
[build comparison](docs/builds.md).

## Run

```sh
papo-tui
```

Enter the instance URL and your credentials. Use **Tab** to move focus,
**Enter** to confirm and **Ctrl+Q** to quit.

The [usage guide](docs/usage.md) covers essential controls and common problems.

Press **Ctrl+S**, or click the gear beside your name below the channels, to choose
an application mode: **Normal** uses terminal graphics with a Chafa fallback;
**Light** draws images and animated GIFs with Chafa characters and disables
video; **Ultralight** uses text only and releases the media libraries.
Video decoding can use compatible GPU drivers, with automatic CPU fallback;
animated media also benefit from parallel Chafa processing in text terminals.

## Build

See [BUILD.md](BUILD.md) for the packages to install and the commands to build
the Linux binary or Windows executable, run tests and prepare packages.
The service contract is in [openapi.yml](openapi.yml).

## Releases and license

[Changelog](CHANGELOG.md) · [Full release notes](releases/v0.4.0.md)

Original code is licensed under [MIT](LICENSE). Bundled libraries and data
retain their [upstream licenses](THIRD_PARTY_NOTICES.md).

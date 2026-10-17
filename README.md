# Papo TUI

Terminal chat client written in C. Connect to independent Papo instances and
chat in channels or direct messages, with message editing, deletion, replies,
reactions, pinned messages, server message search, notifications, mentions, emojis
and media in supported terminals.

Attach files with **F5** or **📎**, or paste copied files and images with
**Ctrl+V** when the local clipboard is available.
Available channels and message actions follow your account permissions.
Member names follow their role color, with white as the default.

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

Press **F2**, or click the gear beside your name below the channels, to choose
an application mode: **Normal** uses terminal graphics with a Chafa fallback;
**Light** draws images and animated GIFs with Chafa characters and disables
video; **Ultralight** uses text only and releases the media libraries.

## Build

Install the [dependencies](docs/installation.md#build-from-source) and run:

```sh
cmake -S . -B build -DBUILD_TESTING=OFF
cmake --build build
./build/papo-tui
```

To contribute or run tests, see [CONTRIBUTING.md](CONTRIBUTING.md).
The service contract is in [openapi.yml](openapi.yml).

## Releases and license

[Changelog](CHANGELOG.md) · [Full release notes](releases/v0.4.0.md)

Original code is licensed under [MIT](LICENSE). Bundled libraries and data
retain their [upstream licenses](THIRD_PARTY_NOTICES.md).

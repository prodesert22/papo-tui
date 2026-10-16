# Development

The application uses C17. Read AGENTS.md before changing the code. The project
includes Windows adapters, packaging and validation; client releases do not
change the OpenAPI contract or add backend features.

## Dependencies and decisions

We reuse GLib/GIO for Unicode, clocks, tasks and native notifications.
Win32 provides Credential Manager, ACLs and console/clipboard operations.
These are platform-maintained APIs available in C; no C++ notification library
or custom password storage is needed. pywinpty is used only by Windows tests
with ConPTY. gcovr and Ruff are development tools. Recipes record sources,
versions and hashes.

Windows builds use a private ncurses 6.6 with the compatibility patch in
`dist/patches/ncurses-win32-wide-input.patch`. The upstream console driver passes
UTF-16 units into the byte/key-code queue: `wget_wch` then misreads accented
characters as UTF-8 and other Unicode units as function keys. The patch tags
native non-ASCII units inside that queue and removes the tag in the public
narrow/wide readers. The existing input adapter joins surrogate pairs. It keeps
ncurses sequence and mouse decoding, adds no input parser, and leaves Linux on
its system ncurses. Dependency source kits include the patch and recipe.

Application modes reuse the existing profile configuration, ncurses controls,
media cache, downloads and Chafa dependency. Configuration schema 4 adds
`app_mode`; versions 1–3 migrate with Normal as the default. Settings are saved
atomically before the selected mode is applied to all connected instances.
Appearance, audio, accessibility and shortcut sections are placeholders.

The media libraries run in `papo-backends/papo-media-worker`, using GIO's
subprocess and asynchronous stream APIs on Linux and Windows. Private pipes
carry bounded, versioned GVariant messages; existing codec modules, graphics
encoders, font resources and player controls are reused inside the helper.
Ultralight stops the helper and skips media work. Light uses Chafa's truecolor
cells, preserves cached frames and retires an idle helper after three seconds.
The host maps RGB cells to ncurses color pairs with a bounded reusable table.
Cache reservations include IPC working copies, and cancellation keeps those
reservations until pending replies drain. Emoji search, occurrence storage,
download clients and cache threads are allocated on demand.

Emoji completion and the F4 picker share the existing catalog, media cache and
ncurses controls. GLib supplies normalized search, user configuration paths and
atomic file replacement; Jansson stores bounded usage counts. Counts record
explicit insertions, scoped by instance URL and account ID, with Unicode text
and custom emoji UUIDs as stable keys. No new dependencies or backend APIs are
needed. Menus retain graphics outside their bounds and request 32×32 previews.

Player controls use ncursesw for bars, colors and clicks, and the libmpv API
for pause, position and volume. They add no dependencies or backend calls;
operations remain in the media worker. The `,`/`.` keys (and `<`/`>`) seek by one second
through libmpv's exact seeking, while the arrow keys retain five-second steps.
The player uses asynchronous commands and observes libmpv properties so the
rendering worker does not block the player core. Decoding uses `hwdec=auto-copy`,
falling back to software when hardware is unsuitable. With Kitty, the terminal
scales frames up, avoiding transfers of extra pixels just to fill the modal;
other protocols keep their existing renderer.
When Kitty's terminfo still uses X10, mouse initialization reuses ncurses' SGR
capabilities to handle clicks beyond column 223 while keeping the terminal's
other capabilities and avoiding a separate mouse parser.

Keyboard initialization queries CSI-u support and enables only key
disambiguation after a positive reply, using ncurses `define_key` for decoding.
This lets Kitty distinguish Shift+Enter from Enter. The previous keyboard mode
is restored through the terminal's push/pop stack on exit. Terminals that do
not answer keep their existing keyboard mode and can use Ctrl+N for new lines.

## Validation

```sh
python3 -m venv build/test-env
build/test-env/bin/pip install -r requirements-dev.txt
cmake -S . -B build -DPython3_EXECUTABLE="$PWD/build/test-env/bin/python"
cmake --build build
ctest --test-dir build --output-on-failure
python3 tools/quality.py
```

`PAPO_SANITIZERS=ON` enables ASan/UBSan. `PAPO_COVERAGE=ON` enables GCC
instrumentation; gcovr generates line and branch coverage reports in build/.
There is no minimum coverage percentage. New tests should verify observable
behavior and relevant failures rather than repeat the implementation.
Count test suites and scenarios separately: each CTest entry may run many
unittest cases. Never use personal credentials, keyrings or clipboard data in tests.

On Linux, `tools/check_light_performance.py --binary build/papo-tui
--output build/light-performance.json` measures host/helper RSS and PSS, CPU
and input latency against synthetic local HTTP and PTY data. Its results are
diagnostics for that machine, not guarantees for a remote instance.

Keep commits small and write their messages in English. Keep mechanical
formatting separate from behavior changes. Do not format generated code;
update the emoji catalog and fixtures through their generators.

## Organization

- `src/app`: application lifecycle and state, send queue and sessions.
- `src/config`: profiles and read cursors; persistence through adapters.
- `src/model`: conversations, notifications, text and remote resources.
- `src/net/auth`, `http` and `realtime`: authentication/API, downloads and WebSocket.
- `src/platform/linux` and `windows`: console, files, clipboard, credential store and notifications.
- `src/ui/screens`, `components`, `input` and `media`: screens and presentation.
- `tests/unit`, `integration`, `terminal`, `distribution`, `contract`: tests;
  `tests/support` and `probes` provide the synthetic backend and helper executables.

## Packaging and releases

`dist/dependencies.json`, `resources.json` and `tools.json` pin sources and hashes.
`dist/bootstrap.py --prefix build/release-deps` prepares a private prefix;
`dist/build.py --build build/release --prefix build/release-deps` also configures,
builds and tests. Run these with the virtual environment's Python and put its
executables directory on PATH. On Windows, use native CPython inside the MSYS2
CLANG64/CLANGARM64 shell, as shown in the workflow.

CI uses one build matrix for the four native OS/architecture combinations.
On Linux, `dist/build.py --build build --prefix build/release-deps
--variants release,sanitizers,coverage --jobs 4` prepares dependencies once and
runs independent CMake trees concurrently. `--jobs` is the shared compiler CPU
budget; `--test-jobs` sets the total CTest concurrency across variants (default:
two I/O tests per active variant). All variant results are checked; failures are
never retried automatically. Logs are written to `build/ci-logs/`.

Run these checks locally before pushing. The history terminal suite also limits
the application's stack to 1 MiB, reproducing Windows stack exhaustion on Linux
while exercising login, history, resizing and Unicode message sending. Windows
cross builds and Wine can check compilation and native CMake metadata; ConPTY
interaction and native credential tests still require Windows.

For releases, each Linux runner uses `dist/ci_linux.py --prepare` before cache
restore, then `dist/ci_linux.py --arch x86_64 --version 0.2.0` (or `aarch64`).
This requires Docker and Flatpak/Builder. Ubuntu, Debian, Fedora and Flatpak
build in isolated copies of the committed checkout, with two concurrent tasks
by default (`--workers` overrides this). Compiler jobs are divided between those
tasks, and container prefixes stay at `/workspace/build/release-deps` so restored
libraries retain their paths. Ubuntu x86_64 also runs sanitizers, coverage and
static analysis. Every distribution runs its full CTest suite and package
installation checks; the Ubuntu task also validates the Arch package.
Task logs and elapsed times are in `build/release-tasks/logs/`. Only successful
builds are assembled for publication, using the existing asset collision checks.

`dist/package.py --help` lists native formats. Packaging requires the verified
prefix and includes licenses, corresponding sources and a library inventory.
`dist/flatpak/build.sh ARCHITECTURE VERSION` generates the actual bundle and
its sources; it requires Flatpak/Builder and the Freedesktop 26.08 runtime and SDK.

Before releasing, update the version in CMakeLists.txt, the summary in CHANGELOG.md
and `releases/vMAJOR.MINOR.PATCH.md`. Publish the `vMAJOR.MINOR.PATCH` tag on GitHub.
The Release workflow validates the tag identity, runs CI, builds each architecture,
tests packages and publishes only when the entire matrix passes. Manual runs
accept an existing tag and apply the same checks. Published releases are not
overwritten. `SHA256SUMS` is generated after all assets are collected and verified.

Keep reports and logs in `build/`; do not commit binaries, accounts or cookies.

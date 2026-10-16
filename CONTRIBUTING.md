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

Message navigation and context menus share UUID-based history layouts and hit
regions. Reply targets belong to per-conversation outbox drafts and immutable
send snapshots; retries preserve the captured target. The multipart transport
adds only the documented `reply_to` field. There is no mention-control parameter
in the current contract or inspected backend, so no toggle is exposed.

Replies use a bounded, active-conversation reference cache and sequential cursor
lookups, separate from the displayed history window. There is no documented
single-message GET endpoint. Background lookups therefore use the history
endpoint, whose reads mark remote read state; inactive histories are never
queried. Failed lookups do not imply deletion. Context changes cancel reads;
already transmitted mutations retain their response owner until consumed.

Reactions use the existing authenticated dispatcher and global request budget.
The `react_update` event has an absolute total and no channel or actor. It can
update matching messages in this instance, but cannot establish own membership.
Confirmed mutations and uncertain outcomes reconcile through REST history;
mutations are never replayed automatically. Participant cursors use the oldest
`(created_at, id)` across all groups in a descending page, independently of group
order. Page group counts are not message totals.

The two-column participant panel keeps emoji and user scroll positions separate.
Its emoji totals come from message summaries; the existing paginated reaction
owner supplies users for the selected emoji. It reuses the inline icon renderer
on the overlay layer. Navigation shortcuts (`r`, `e`, `Shift+E`) share the same
reply and reaction actions as the context menu. No dependency or endpoint is added.

Video covers reuse the existing FFmpeg image module in an explicit first-frame
mode, including containers with audio. Normal image decoding retains its codec
allowlist. The module ABI is 8. Files, decoded covers and variants share the
bounded media cache; libmpv opens the same cached file for playback. Workers
perform decoding and never access auth, HTTP or curses. No new dependency or
backend endpoint is required.

Floating emoji/reaction panels split intersecting history images into at most
four visible rectangles. Cropped variants reuse the existing bounded worker
cache and FFmpeg/Chafa encoders, including Unicode icon rasters. The main thread
only queues variants and places them; no decoding or encoding runs in curses.
The media service transports crop bounds and the explicit video-cover flag in
wire version 3. Raster packing and emoji rasterization run only in the helper.
Light keeps native Unicode emojis and skips video covers and playback;
Ultralight keeps message actions available without starting the media helper.

`python -m tools.check_message_interactions --url http://127.0.0.1:8080`
validates the C interaction owner against an isolated DM between synthetic
accounts, including an uploaded video. It keeps test credentials private under
build/, removes this run's messages and closes its DM views and sessions.
The sanitized report distinguishes functional checks from contract divergences.

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

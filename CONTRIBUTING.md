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
its system ncurses. The patch also preserves coalesced key repeats and maps
right clicks to terminal button three regardless of the reported mouse hardware.
The input adapter checks the native viewport to catch consumed resize events.
Dependency source kits include the patch and recipe. ConPTY checks exercise
Unicode, repeated input, resizing and message interactions; those interaction
tests select Light mode through the UI so Windows creates the configuration
with its native private permissions.

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

Message editing and deletion reuse ncurses, the bounded text editor, Jansson
and the authenticated REST dispatcher; no dependency is added. Editing uses
`PUT /messages/{message_id}` with only `content`, preserving attachments and
custom embeds. Deletion uses the documented DELETE and server authorization.
The empty-editor Up shortcut searches loaded messages for the newest own
message; it does not fetch inactive histories or promise a server-wide search.
Edit buffers are separate from send drafts and are discarded on context changes.
The editing highlight reuses the existing ncurses message border and UUID-based
scroll positioning, without new dependencies or requests.
Both mutations share the global rate budget and retain their response owner
across conversation changes. Failures require an explicit retry; uncertain
outcomes refresh the active history without replaying the mutation.

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

Floating menus and dialogs split intersecting history images into at most
four visible rectangles. Cropped variants reuse the existing bounded worker
cache and FFmpeg/Chafa encoders, including Unicode icon rasters. The main thread
only queues variants and places them; no decoding or encoding runs in curses.
Mention and notification panels return their borrowed ncurses windows so the
conversation registers their bounds before composing image and icon placements.
The media service transports crop bounds and the explicit video-cover flag in
wire version 3. Raster packing and emoji rasterization run only in the helper.
Light keeps native Unicode emojis and skips video covers and playback;
Ultralight keeps message actions available without starting the media helper.

Animated server emojis reuse FFmpeg's GIF/APNG decoders and its `webp_anim`
decoder when available, along with the bounded shared frame/variant cache, with
no new dependency or endpoint. Light emoji placements
request the first frame; that flag participates in variant identity so a static
emoji cannot freeze an attached GIF with identical bytes. Normal keeps the
existing animation clock. The emoji catalog accepts up to 2 MiB blobs
and 2048-pixel dimensions, and its HTTP response budget accommodates a full
25-item base64 page. Existing per-instance temporary-file budgets still apply.

Emoji-only message bodies reuse the icon resolver and shared cache at 48 pixels.
Their rows retain source offsets for reading anchors and repeat icon regions so
the viewport can crop partially visible emojis through the existing image path.
No dependency or backend request is added. Mixed text, reactions and native
Unicode rendering in Light/Ultralight keep their ordinary layout.

The notification center reuses the mention projector, libc sorting and session
timestamp comparison for local search, conversation filters and date ordering.
Search and counters cover loaded pages; the API exposes no global unread total.
Mention badges count explicit user/everyone mentions in available content;
truncated previews can omit a mention. General unread messages retain a separate
dot. Notification payloads lack webhook names and event timestamps/channels;
bounded metadata from WS and already loaded active-history messages supplies these
when known, including mention flags beyond the truncated preview.
Missing metadata stays explicit, without fetching inactive histories or inventing
an individual-message endpoint. No new dependency is needed.
Notification arrows distinguish text editing from list/filter actions: horizontal
list keys change the filter, vertical keys traverse search, controls and results.
Server badges share tab measurement and drawing, reserve the closing delimiter
and use plain red counters when the effective renderer is text/Chafa.
Channel/DM filter selection uses a searchable subview in the same borrowed modal
window. It reuses bounded text editing/name matching and already loaded directory
metadata, with UUID selection anchors across updates; no history reads or new
dependencies are required.
Category arrows/clicks only cycle the filter; Enter or the separate Name button
explicitly opens the selector, avoiding focus changes while cycling categories.
The Name button follows the rendered filter label by one cell; drawing and mouse
hit testing use the same Unicode cell measurement while the date action retains
its reserved column.

`python -m tools.check_message_interactions --url http://127.0.0.1:8080`
validates the C interaction owner against an isolated DM between synthetic
accounts, including queued-edit cancellation, editing, deletion and an uploaded
video. It keeps test credentials private under
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

For workflow changes, install ShellCheck and run
`go run github.com/rhysd/actionlint/cmd/actionlint@v1.7.12`.
ShellCheck must be available on PATH: actionlint otherwise omits shell checks,
so a local success would not exercise the same checks as the GitHub runner.

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
Development uses a permanent `dev` branch. Commit changes and prepare the next
version there, then open a pull request targeting `main`. Routine CI runs on
pull requests, including version commits; each new commit cancels the previous
run for that PR. The `quality` and four `Build (...)` check names are preserved
for branch protection. Test executables are compiled, but distribution packages
are generated only by the release pipeline. Pushes to `dev` do not start CI
without an open PR, and pushes to `main` do not repeat routine CI.

Merge `dev` into `main` with a merge commit. Before starting the next delivery,
fetch and merge `origin/main` into `dev` (fast-forward when possible), retaining
any newer development commits. Keep `dev`; do not force push either branch.
Configure main protection to require a PR and the existing CI checks. Main
must allow merge commits; do not enable a linear-history requirement for this
permanent-branch workflow. Branch protection is managed by the maintainer.
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
restore, then `dist/ci_linux.py --arch x86_64 --version 0.3.1` (or `aarch64`).
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

Prepare each release in the PR: update CMakeLists.txt, CHANGELOG.md and
`releases/vMAJOR.MINOR.PATCH.md`. Version-preparation commit titles start with
`Release MAJOR.MINOR.PATCH: ...` and use English. PR checks validate increasing
versions and the required release notes. Commit titles do not bypass PR tests.

After the merge, the Release workflow compares the CMake version with the
previous main revision. An unchanged version finishes without packaging. A new
version creates an annotated `vMAJOR.MINOR.PATCH` tag on the merged commit, then
calls the existing native release pipeline directly. The bot writes tags and
release assets, never commits to main. Releases are serialized and are not
cancelled by later PR commits. Automatic publication does not depend on another
workflow being triggered by the bot-created tag.

Every release builds and tests each target and verifies installation before
publishing. The publish job downloads those same artifacts without rebuilding.
Packages include licenses, corresponding source archives and `SHA256SUMS`.
Published releases and existing tags are never replaced or moved.

If an environment or packaging check fails, rerun failed jobs or manually run
Release with the existing tag. The tag must point to the checked-out commit and
be reachable from main; a conflicting tag or an already published release is
rejected. To release code fixes after a failed tagged build, prepare a higher
version in another PR instead of moving the failed version's tag.

Keep reports and logs in `build/`; do not commit binaries, accounts or cookies.

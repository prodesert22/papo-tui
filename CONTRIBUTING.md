# Development

The application uses C17. Read AGENTS.md before changing the code. Preparation
for 0.1.0 includes Windows adapters, packaging and validation; it does not
change the OpenAPI contract or add backend features.

## Dependencies and decisions

We reuse GLib/GIO for Unicode, clocks, tasks and native notifications.
Win32 provides Credential Manager, ACLs and console/clipboard operations.
These are platform-maintained APIs available in C; no C++ notification library
or custom password storage is needed. pywinpty is used only by Windows tests
with ConPTY. gcovr and Ruff are development tools. Recipes record sources,
versions and hashes.

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

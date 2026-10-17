# Local Windows testing over SSH

These instructions use the existing Windows x64 environment on Pedro's PC.
They run tests locally without starting GitHub Actions.

## Prepared environment

- SSH: `pedro@192.168.31.86`.
- Repository: `C:\Users\Pedro\Documents\papo-tui`.
- MSYS2: `C:\msys64`, using CLANG64.
- Native Python environment: `build\test-env` (Python 3.13).
- Application build: `build\release`.
- Private dependencies: `build\release-deps`.
- Local runners and logs: `build\local-windows`.

The runners are local files under the ignored `build` directory. They already
exist on this PC; cloning the repository elsewhere does not install them.
Copies used during setup are available on the Linux workstation under
`build/windows-local/scripts/`.

## Connect

Keep Pedro signed in to the Windows desktop, and connect from Linux:

```sh
ssh -t -i ~/.ssh/id_ed25519 pedro@192.168.31.86 powershell.exe -NoProfile
```

Unlock the SSH key locally if requested. Never copy the private key or its
passphrase to Windows or into this repository.

In the remote PowerShell session:

```powershell
cd C:\Users\Pedro\Documents\papo-tui
git status
```

If the working tree is clean, update the current branch when needed:

```powershell
git pull --ff-only
```

Preserve any uncommitted work before updating. Check which branch and changes
you are testing; files copied for local validation may differ from GitHub.

## Start a test run

Incrementally build the application and run the full CTest suite:

```powershell
powershell -ExecutionPolicy Bypass -File .\build\local-windows\start-interactive.ps1 -Mode test
```

For terminal interaction tests only:

```powershell
powershell -ExecutionPolicy Bypass -File .\build\local-windows\start-interactive.ps1 -Mode terminal
```

If the build or private dependencies need to be configured or rebuilt, use:

```powershell
powershell -ExecutionPolicy Bypass -File .\build\local-windows\start-interactive.ps1 -Mode build
```

The `build` mode runs the distribution build script and its tests. The `test`
and `terminal` modes reuse the configured build and compile changed sources.
Run one test round at a time.

The launcher starts an on-demand scheduled task and returns immediately.
It uses the signed-in user's interactive token, without storing a password or
installing an automatic schedule. This is necessary because a public-key SSH
session on this machine cannot access Windows Credential Manager: direct
execution previously returned Windows error 1312 during credential tests.

A terminal window may appear on the Windows desktop when the task launches
MSYS2. This comes from the local runner, not from SSH itself. Do not close it
while a run is active.

## Follow the log

For a full test run:

```powershell
$log = Get-ChildItem .\build\local-windows\logs\test-*.log |
    Sort-Object LastWriteTime -Descending |
    Select-Object -First 1

Get-Content $log.FullName -Tail 30 -Wait
```

For other modes, replace `test-*.log` with `terminal-*.log` or `build-*.log`.
If no new log exists yet, wait for the task to start and repeat the command.
Press Ctrl+C to stop following the log; this does not stop the test task.

## Check completion

```powershell
Get-ScheduledTask -TaskName 'PapoTUI-LocalTests-*' |
    Select-Object TaskName, State

Get-ScheduledTaskInfo -TaskName 'PapoTUI-LocalTests-5E78C2C48173' |
    Select-Object LastRunTime, LastTaskResult
```

For the configured clone, `Ready` and `LastTaskResult = 0` indicate that the
task finished successfully. Confirm that `LastRunTime` belongs to the run you
just started and that its log shows `100% tests passed`. At the time these
instructions were written, the full Windows suite contained 18 CTest tests.
While the task is running, its result is not a final exit status.

The task name is derived from the repository path. A different clone path
produces a different suffix; use the name printed by the launcher.

Detailed CTest results are in:

```text
build\release\Testing\Temporary\LastTest.log
```

Preserve a failing run's logs before starting another run, since CTest replaces
its latest-run files.

## Scope of validation

This machine provides native Windows x64 tests through ConPTY. Passing here
does not establish that native Windows ARM64 passes, nor replace visual checks
in a real terminal emulator when changing rendering.

"""Look inside a running process: its children, command line and mapped libraries.

Linux reads /proc. macOS has none, so pgrep, ps and lsof answer instead. A process that has gone
raises FileNotFoundError or ProcessLookupError on either system; callers that race with exiting
children catch both.
"""

from pathlib import Path
import subprocess
import sys


def _ask(*command):
    result = subprocess.run(command, capture_output=True, text=True, errors="replace")
    if result.returncode != 0 and not result.stdout:
        raise ProcessLookupError(" ".join(command))
    return result.stdout


def children(pid):
    """Pids of the processes that pid started, whichever of its threads did so."""
    if sys.platform.startswith("linux"):
        found = set()
        for task in Path(f"/proc/{pid}/task").iterdir():
            # A thread or child that exits between the listing and the read is gone:
            # /proc then answers ENOENT or, while the process is being reaped, ESRCH.
            try:
                found.update(int(child) for child in (task / "children").read_text().split())
            except (FileNotFoundError, ProcessLookupError):
                continue
        return sorted(found)
    # pgrep exits with 1 and prints nothing when there is no child.
    result = subprocess.run(["pgrep", "-P", str(pid)], capture_output=True, text=True)
    return sorted(int(child) for child in result.stdout.split())


def command_line(pid):
    """What pid was started with, as bytes."""
    if sys.platform.startswith("linux"):
        return Path(f"/proc/{pid}/cmdline").read_bytes()
    return _ask("ps", "-o", "command=", "-p", str(pid)).encode()


def mapped_files(pid):
    """The files mapped into pid, one per line: library names are what the tests look for."""
    if sys.platform.startswith("linux"):
        return Path(f"/proc/{pid}/maps").read_text(encoding="utf-8")
    return _ask("lsof", "-nP", "-p", str(pid))

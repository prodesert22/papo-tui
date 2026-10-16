"""Run the application on an emulator's real PTY and record restoration only."""

import argparse
import json
import os
from pathlib import Path
import signal
import subprocess
import termios
import time


def write(path, result):
    temporary = path.with_suffix(".tmp")
    with os.fdopen(os.open(temporary, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600), "w") as output:
        json.dump(result, output)
    temporary.replace(path)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--report", type=Path, required=True)
    parser.add_argument("--isolated-session", action="store_true")
    parser.add_argument("binary", type=Path)
    parser.add_argument("application_args", nargs=argparse.REMAINDER)
    arguments = parser.parse_args()
    arguments.report.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
    original = termios.tcgetattr(0)
    stopping = False

    def stop(_signal, _frame):
        nonlocal stopping
        stopping = True

    signal.signal(signal.SIGTERM, stop)
    environment = dict(os.environ)
    if arguments.isolated_session:
        environment["DBUS_SESSION_BUS_ADDRESS"] = "unix:path=/nonexistent/papo-test-bus"
    child = subprocess.Popen(
        [str(arguments.binary.resolve()), *arguments.application_args], env=environment
    )
    result = {"pid": child.pid, "term": os.environ.get("TERM"), "running": True}
    try:
        while child.poll() is None and not stopping:
            size = os.get_terminal_size(0)
            result.update(rows=size.lines, columns=size.columns)
            write(arguments.report, result)
            time.sleep(0.08)
    finally:
        if child.poll() is None:
            child.send_signal(signal.SIGTERM)
            try:
                child.wait(timeout=5)
            except subprocess.TimeoutExpired:
                child.kill()
                child.wait(timeout=5)
        result.update(
            running=False, returncode=child.returncode, restored=termios.tcgetattr(0) == original
        )
        write(arguments.report, result)
    print("Terminal restoration: " + ("passed" if result["restored"] else "failed"))
    return 0 if result["restored"] and child.returncode == 0 else 2


if __name__ == "__main__":
    raise SystemExit(main())

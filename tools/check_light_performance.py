"""Measure Linux host/worker RAM, CPU and input latency with synthetic PTY data.

Uses isolated configuration and local mock HTTP. Reports no credentials or
remote data. RSS includes shared pages; PSS apportions those pages per process.
"""

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import select
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]


def children(pid):
    found = set()
    for task in Path(f"/proc/{pid}/task").iterdir():
        try:
            pids = (task / "children").read_text().split()
        except FileNotFoundError:
            continue
        for child in pids:
            try:
                if b"papo-media-worker" in Path(f"/proc/{child}/cmdline").read_bytes():
                    found.add(int(child))
            except FileNotFoundError:
                pass
    return sorted(found)


def usage(pid):
    values = {}
    for line in Path(f"/proc/{pid}/smaps_rollup").read_text().splitlines():
        if line.startswith(("Rss:", "Pss:", "Private_Clean:", "Private_Dirty:")):
            key, value, *_ = line.split()
            values[key[:-1] + "_KiB"] = int(value)
    stat = Path(f"/proc/{pid}/stat").read_text().rsplit(")", 1)[1].split()
    values["cpu_ticks"] = int(stat[11]) + int(stat[12])
    categories = {}
    category = "anonymous"
    for line in Path(f"/proc/{pid}/smaps").read_text().splitlines():
        if "-" in line.split()[0]:
            fields = line.split()
            name = fields[5] if len(fields) > 5 else ""
            category = Path(name).name if name.startswith("/") else "anonymous"
        elif line.startswith("Rss:"):
            categories[category] = categories.get(category, 0) + int(line.split()[1])
    values["rss_by_mapping_KiB"] = dict(sorted(categories.items(), key=lambda pair: -pair[1]))
    return values


def snapshot(t):
    processes = [usage(t.process.pid)]
    for pid in children(t.process.pid):
        try:
            processes.append(usage(pid))
        except (FileNotFoundError, ProcessLookupError):
            pass
    return {
        "host": processes[0],
        "workers": processes[1:],
        "rss_KiB": sum(p["Rss_KiB"] for p in processes),
        "pss_KiB": sum(p["Pss_KiB"] for p in processes),
        "cpu_ticks": sum(p["cpu_ticks"] for p in processes),
    }


def cpu_counters(t):
    result = {}
    for pid in [t.process.pid, *children(t.process.pid)]:
        try:
            stat = Path(f"/proc/{pid}/stat").read_text().rsplit(")", 1)[1].split()
            result[pid] = int(stat[11]) + int(stat[12])
        except FileNotFoundError:
            pass
    return result


def cpu_delta(before, after):
    return sum(max(0, ticks - before.get(pid, 0)) for pid, ticks in after.items())


def read_for(t, seconds):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        ready, _, _ = select.select([t.master], [], [], max(0, deadline - time.monotonic()))
        if ready:
            data = os.read(t.master, 65536)
            t.output.extend(data)
            t.stream.feed(data)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=ROOT / "build/papo-tui")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--idle-seconds", type=float, default=3)
    args = parser.parse_args()
    if not math.isfinite(args.idle_seconds) or args.idle_seconds <= 0:
        parser.error("--idle-seconds must be positive and finite")

    for directory in ("support", "terminal", "integration"):
        sys.path.insert(0, str(ROOT / "tests" / directory))
    previous = sys.argv[:]
    sys.argv = [sys.argv[0], str(args.binary.resolve()), "ffmpeg"]
    try:
        from test_media_terminal import MediaTerminal
        from history_mock import uuid
        import test_terminal
    finally:
        sys.argv = previous
    test_terminal.BINARY = str(args.binary.resolve())
    MediaTerminal.setUpClass()
    report = {
        "kind": "synthetic-http-and-pty",
        "binary_sha256": hashlib.sha256(args.binary.read_bytes()).hexdigest(),
        "idle_seconds": args.idle_seconds,
        "measurements": [],
    }
    try:
        with tempfile.TemporaryDirectory(prefix="papo-mode-performance-") as directory:
            folder = Path(directory)
            image = folder / "image.png"
            animated = folder / "animation.gif"
            for path, source, extra in (
                (image, "testsrc2=s=1920x1080:r=1", ["-frames:v", "1"]),
                (animated, "testsrc2=s=320x180:r=15", ["-t", "2"]),
            ):
                subprocess.run(
                    [
                        "ffmpeg",
                        "-v",
                        "error",
                        "-f",
                        "lavfi",
                        "-i",
                        source,
                        *extra,
                        "-threads",
                        "1",
                        "-y",
                        str(path),
                    ],
                    check=True,
                    capture_output=True,
                )
            for mode in ("ultralight", "light"):
                for scenario, data, mime in (
                    ("text", b"", "image/png"),
                    ("image", image.read_bytes(), "image/png"),
                    ("animation", animated.read_bytes(), "image/gif"),
                ):
                    helper = MediaTerminal()
                    config_home = folder / f"{mode}-{scenario}"
                    config = config_home / "papo-tui/config.json"
                    config.parent.mkdir(parents=True)
                    config.write_text(
                        json.dumps(
                            dict(
                                version=4,
                                profiles=[],
                                selected_profile=None,
                                requests_per_second=10,
                                request_burst=25,
                                desktop_notifications=False,
                                notification_preview=False,
                                app_mode=mode,
                            )
                        )
                    )
                    with helper.serve(data, mime) as server:
                        first = dict(server.messages[0])
                        server.count = 100
                        server.messages = [
                            dict(
                                first,
                                id=uuid(101 + i),
                                content=f"Message {i:03d}: synthetic text without remote data.",
                                attachments=first["attachments"]
                                if i == 99 and scenario != "text"
                                else [],
                            )
                            for i in range(100)
                        ]
                        t = helper.open(
                            server,
                            sixel=False,
                            size=(24, 100),
                            environment={
                                "XDG_CONFIG_HOME": str(config_home),
                                "XDG_STATE_HOME": str(config_home / "state"),
                            },
                        )
                        try:
                            t.read(2)

                            def visible_colors():
                                return {
                                    color
                                    for y in range(8, 16)
                                    for x in range(32, 97)
                                    for color in (
                                        t.screen.buffer[y][x].fg,
                                        t.screen.buffer[y][x].bg,
                                    )
                                }

                            if mode == "light" and scenario != "text":
                                deadline = time.monotonic() + 10
                                while len(visible_colors()) <= 8 and time.monotonic() < deadline:
                                    t.read(0.03)
                                if len(visible_colors()) <= 8:
                                    raise AssertionError("Synthetic media did not render")
                            counters = cpu_counters(t)
                            idle_ticks = 0
                            began = time.monotonic()
                            while time.monotonic() - began < args.idle_seconds:
                                t.read(min(0.1, args.idle_seconds - (time.monotonic() - began)))
                                current = cpu_counters(t)
                                idle_ticks += cpu_delta(counters, current)
                                counters = current
                            end = snapshot(t)
                            idle_cpu = (
                                100
                                * idle_ticks
                                / (os.sysconf("SC_CLK_TCK") * (time.monotonic() - began))
                            )
                            latencies = []
                            typing_ticks = 0
                            typed_text = ""
                            for n in range(30):
                                began = time.monotonic()
                                letter = chr(ord("a") + n % 26)
                                typed_text += letter
                                os.write(t.master, letter.encode())
                                deadline = began + 3
                                while not any("> " + typed_text in row for row in t.screen.display):
                                    read_for(t, 0.001)
                                    if time.monotonic() > deadline:
                                        raise AssertionError(f"Input sample {n} did not appear")
                                latencies.append((time.monotonic() - began) * 1000)
                                current = cpu_counters(t)
                                typing_ticks += cpu_delta(counters, current)
                                counters = current
                            result = dict(
                                mode=mode,
                                scenario=scenario,
                                idle=end,
                                idle_cpu_percent=round(idle_cpu, 2),
                                typing_cpu_ms=round(
                                    1000 * typing_ticks / os.sysconf("SC_CLK_TCK"), 2
                                ),
                                input_p95_ms=round(
                                    sorted(latencies)[math.ceil(0.95 * len(latencies)) - 1], 2
                                ),
                                media_requests=sum(
                                    "/attachments/" in r[1] for r in server.requests
                                ),
                            )
                            if mode == "ultralight" and (
                                result["media_requests"] or end["workers"]
                            ):
                                raise AssertionError("Ultralight started media processing")
                            report["measurements"].append(result)
                            print(
                                json.dumps({k: v for k, v in result.items() if k != "idle"}),
                                flush=True,
                            )
                        finally:
                            assert t.close()
                            helper.doCleanups()
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(json.dumps(report, indent=2) + "\n")
    finally:
        MediaTerminal.tearDownClass()


if __name__ == "__main__":
    main()

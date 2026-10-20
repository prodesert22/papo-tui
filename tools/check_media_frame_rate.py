"""Compare synthetic playback cadence; this is PTY evidence, not emulator inspection."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", type=Path, required=True)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument(
        "--large", action="store_true", help="also measure larger media and viewport"
    )
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    os.environ.setdefault("PAPO_TEST_ASSETS", str(root / "build/test-assets"))
    sys.path.insert(0, str(root / "tests/support"))
    from media_cadence import measure
    import terminal_media
    import terminal_support

    ffmpeg = shutil.which("ffmpeg")
    if not ffmpeg:
        parser.error("ffmpeg executable is required")
    terminal_media.FFMPEG = ffmpeg
    report = dict(
        kind="synthetic HTTP + PTY",
        system=platform.platform(),
        processors=os.cpu_count(),
        build="Release",
        runs={},
    )
    if hasattr(os, "sched_getaffinity"):
        report["cpu_affinity"] = len(os.sched_getaffinity(0))
    quota = Path("/sys/fs/cgroup/cpu.max")
    if quota.is_file():
        report["cpu_quota"] = quota.read_text(encoding="utf-8").strip()
    scenarios = [
        (video, protocol, None, None)
        for video, protocols in (
            (False, ("kitty", "sixel", "iterm2", "chafa", "light")),
            (True, ("kitty", "chafa")),
        )
        for protocol in protocols
    ]
    if args.large:
        scenarios += [
            (False, "kitty", "320x180", (50, 160)),
            (True, "kitty", "1280x720", (50, 160)),
        ]
    terminal_media.MediaTerminal.setUpClass()
    try:
        for label, binary in (("before", args.baseline), ("after", args.binary)):
            terminal_support.BINARY = str(binary.resolve())
            run = dict(
                sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),
                backends={
                    path.name: hashlib.sha256(path.read_bytes()).hexdigest()
                    for path in sorted((binary.parent / "papo-backends").iterdir())
                    if path.is_file()
                },
                samples=[],
            )
            for video, protocol, source_size, terminal_size in scenarios:
                test = terminal_media.MediaTerminal()
                try:
                    sample = measure(
                        test,
                        ffmpeg,
                        video=video,
                        protocol=protocol,
                        source_size=source_size,
                        terminal_size=terminal_size,
                        require_input=False,
                    )
                    run["samples"].append(sample)
                    print(label, sample, flush=True)
                finally:
                    test.doCleanups()
            report["runs"][label] = run
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    finally:
        terminal_media.MediaTerminal.tearDownClass()


if __name__ == "__main__":
    main()

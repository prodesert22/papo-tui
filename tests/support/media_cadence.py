"""Synthetic motion observed through PTY bytes or sampled Chafa cells."""

import json
import os
from pathlib import Path
import subprocess
import tempfile
import time


def measure(
    test,
    ffmpeg,
    *,
    video,
    protocol,
    seconds=2,
    source_size=None,
    terminal_size=None,
    require_input=True,
):
    with tempfile.TemporaryDirectory(prefix="papo-cadence-") as directory:
        root = Path(directory)
        path = root / ("motion.mp4" if video else "motion.gif")
        command = [ffmpeg, "-v", "error", "-f", "lavfi", "-i"]
        source_size = source_size or ("160x96" if video else "96x64")
        command += [f"testsrc2=s={source_size}:r={60 if video else 50}"]
        command += ["-t", "8" if video else "2", "-threads", "1"]
        command += ["-c:v", "mpeg4", "-an"] if video else ["-loop", "0"]
        subprocess.run([*command, "-y", str(path)], check=True, capture_output=True)
        environment = {"XDG_CONFIG_HOME": str(root), "TERM": "xterm-256color"}
        if protocol == "light":
            config = root / "papo-tui/config.json"
            config.parent.mkdir()
            config.write_text(json.dumps(dict(version=4, profiles=[], app_mode="light")))
            config.chmod(0o600)
        graphical = protocol in ("kitty", "sixel", "iterm2")
        with test.serve(path.read_bytes(), "video/mp4" if video else "image/gif") as server:
            t = test.open(
                server,
                sixel=graphical,
                protocol=protocol if graphical else None,
                environment=environment,
                size=terminal_size,
            )
            try:
                editor_focus_confirmed = None
                if graphical:
                    test.wait_frames(t)
                else:
                    deadline = time.monotonic() + 8
                    while not any("/attachments/" in r[1] for r in server.requests):
                        if time.monotonic() >= deadline:
                            raise AssertionError("Motion fixture was not requested")
                        t.read(0.01)
                if video:
                    t.send("\x0b\n")
                    test.wait(t, "Reproduzindo")
                else:
                    row, line = next(
                        (y, line)
                        for y, line in enumerate(t.screen.display)
                        if "Conversar em #geral" in line
                    )
                    t.click(row, line.index("Conversar em #geral"))
                    deadline = time.monotonic() + 2
                    while (
                        t.screen.cursor.hidden or t.screen.cursor.y != row
                    ) and time.monotonic() < deadline:
                        t.read(0.004)
                    editor_focus_confirmed = not t.screen.cursor.hidden and t.screen.cursor.y == row
                    if require_input:
                        test.assertTrue(editor_focus_confirmed, "Editor did not receive focus")
                # Exclude initial downloads, decode and layout from steady playback.
                t.read(0.4)
                start_frames = len(t.stream.frames)
                start_bytes = len(t.output)
                start = time.monotonic()
                last = None
                changes = 0
                seen = set()
                while time.monotonic() - start < seconds:
                    t.read(0.004)
                    if not graphical:
                        # Only the media area: omit editor, status and timestamps.
                        snapshot = tuple(
                            (
                                t.screen.buffer[y][x].data,
                                t.screen.buffer[y][x].fg,
                                t.screen.buffer[y][x].bg,
                            )
                            for y in range(8, 16)
                            for x in range(32, 75)
                        )
                        if last is not None and snapshot != last:
                            changes += 1
                        seen.add(snapshot)
                        last = snapshot
                elapsed = time.monotonic() - start
                end_frames = len(t.stream.frames)
                terminal_bytes = len(t.output) - start_bytes
                frames = len(t.stream.frames) - start_frames if graphical else changes
                if graphical:
                    seen = set(t.stream.frames[start_frames:end_frames])
                sent = time.monotonic()
                os.write(t.master, b" " if video else b"cadence-input")
                expected = "Pausado" if video else "cadence-input"
                while expected not in "\n".join(t.screen.display):
                    if time.monotonic() - sent > 2:
                        if require_input:
                            raise AssertionError("Keyboard input stalled during playback")
                        break
                    t.read(0.004)
                latency = time.monotonic() - sent
                responded = expected in "\n".join(t.screen.display)
                return dict(
                    protocol=protocol,
                    media="video" if video else "gif",
                    source_size=source_size,
                    terminal_size=terminal_size or (24, 80),
                    updates=frames,
                    distinct_states=len(seen),
                    seconds=elapsed,
                    updates_per_second=frames / elapsed,
                    fps=frames / elapsed if graphical else None,
                    terminal_bytes=terminal_bytes,
                    raster_bytes=sum(
                        len(frame) for frame in t.stream.frames[start_frames:end_frames]
                    ),
                    keyboard_ms=latency * 1000 if responded else None,
                    keyboard_responded=responded,
                    editor_focus_confirmed=editor_focus_confirmed,
                    observer="PTY graphics frames" if graphical else "sampled TTY cells",
                )
            finally:
                test.assertTrue(t.close())

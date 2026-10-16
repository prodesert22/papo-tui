"""Exercise SIMD buffer boundaries without relying on library instrumentation."""

from pathlib import Path
import subprocess
import sys
import tempfile

with tempfile.TemporaryDirectory(prefix="papo-image-buffer-") as directory:
    path = Path(directory) / "source.webp"
    subprocess.run(
        [
            sys.argv[3],
            "-v",
            "error",
            "-f",
            "lavfi",
            "-i",
            "testsrc2=size=1980x1092",
            "-frames:v",
            "1",
            "-threads",
            "1",
            "-y",
            str(path),
        ],
        check=True,
        capture_output=True,
    )
    subprocess.run([sys.argv[1], sys.argv[2], str(path)], check=True)

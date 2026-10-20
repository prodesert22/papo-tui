"""Immutable FFmpeg fixtures prepared once by CTest and copied into each case."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
from types import SimpleNamespace


FILES = {"tall_image": "tall.png", "animated_gif": "animated.gif", "video": "video.mp4"}


def identity(ffmpeg):
    stat = Path(ffmpeg).stat()
    return hashlib.sha256(
        (
            Path(__file__).read_text(encoding="utf-8")
            + str(Path(ffmpeg).resolve())
            + str(stat.st_size)
            + str(stat.st_mtime_ns)
        ).encode()
    ).hexdigest()


def prepare(directory, ffmpeg):
    directory = Path(directory)
    key = identity(ffmpeg)
    target = directory / key
    if (target / "complete.json").is_file():
        return target
    directory.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="preparing-", dir=directory) as temporary:
        destination = Path(temporary) / "assets"
        destination.mkdir()
        cls = SimpleNamespace(assets=SimpleNamespace(name=str(destination)))
        generate(cls, ffmpeg)
        manifest = {
            p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in destination.iterdir()
        }
        (destination / "complete.json").write_text(json.dumps(manifest), encoding="utf-8")
        try:
            destination.rename(target)
        except OSError:
            if not (target / "complete.json").is_file():
                raise
    return target


def load(cls, ffmpeg):
    cache = os.environ.get("PAPO_TEST_ASSETS", str(Path.cwd() / "test-assets"))
    source = prepare(cache, ffmpeg)
    manifest = json.loads((source / "complete.json").read_text(encoding="utf-8"))
    cls.assets = tempfile.TemporaryDirectory(prefix="papo-media-case-")
    destination = Path(cls.assets.name)
    for name, expected in manifest.items():
        content = (source / name).read_bytes()
        if hashlib.sha256(content).hexdigest() != expected:
            raise RuntimeError("Corrupt synthetic media fixture: " + name)
        (destination / name).write_bytes(content)
    cls.images = {
        ext: (destination / ("image." + ext)).read_bytes() for ext in ("png", "jpg", "webp", "gif")
    }
    cls.large_images = {
        shape: (destination / (shape + ".png")).read_bytes() for shape in ("landscape", "portrait")
    }
    for field, filename in FILES.items():
        setattr(cls, field, (destination / filename).read_bytes())


def generate(cls, FFMPEG):
    cls.images = {}
    for ext in ("png", "jpg", "webp", "gif"):
        path = Path(cls.assets.name) / ("image." + ext)
        subprocess.run(
            [
                FFMPEG,
                "-v",
                "error",
                "-f",
                "lavfi",
                "-i",
                "color=c=red:s=16x12",
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
        cls.images[ext] = path.read_bytes()
    path = Path(cls.assets.name) / "tall.png"
    subprocess.run(
        [
            FFMPEG,
            "-v",
            "error",
            "-f",
            "lavfi",
            "-i",
            "color=c=red:s=16x96",
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
    cls.tall_image = path.read_bytes()
    cls.large_images = {}
    for shape, size in (("landscape", "1000x500"), ("portrait", "1000x1500")):
        path = Path(cls.assets.name) / (shape + ".png")
        subprocess.run(
            [
                FFMPEG,
                "-v",
                "error",
                "-f",
                "lavfi",
                "-i",
                "testsrc2=size=" + size,
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
        cls.large_images[shape] = path.read_bytes()
    path = Path(cls.assets.name) / "animated.gif"
    subprocess.run(
        [
            FFMPEG,
            "-v",
            "error",
            "-f",
            "lavfi",
            "-i",
            "color=red:s=16x12:d=0.2:r=5",
            "-f",
            "lavfi",
            "-i",
            "color=blue:s=16x12:d=0.2:r=5",
            "-filter_complex",
            "[0:v][1:v]concat=n=2:v=1:a=0",
            "-frames:v",
            "2",
            "-loop",
            "0",
            "-threads",
            "1",
            "-y",
            str(path),
        ],
        check=True,
        capture_output=True,
    )
    cls.animated_gif = path.read_bytes()
    path = Path(cls.assets.name) / "video.mp4"
    subprocess.run(
        [
            FFMPEG,
            "-v",
            "error",
            "-f",
            "lavfi",
            "-i",
            "color=c=blue:s=32x24:r=10",
            "-f",
            "lavfi",
            "-i",
            "sine=frequency=440:sample_rate=44100",
            "-t",
            "3",
            "-c:v",
            "mpeg4",
            "-c:a",
            "aac",
            "-movflags",
            "+faststart",
            "-threads",
            "1",
            "-y",
            str(path),
        ],
        check=True,
        capture_output=True,
    )
    cls.video = path.read_bytes()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--directory", type=Path, required=True)
    parser.add_argument("--ffmpeg", required=True)
    args = parser.parse_args()
    target = prepare(args.directory, args.ffmpeg)
    from xml.sax.saxutils import escape

    fonts = Path(__file__).resolve().parents[2] / "build/resources"
    config = '<?xml version="1.0"?><!DOCTYPE fontconfig SYSTEM "fonts.dtd"><fontconfig>'
    config += "<dir>" + escape((fonts / "test-fonts").as_posix()) + "</dir>"
    config += "<dir>" + escape(fonts.as_posix()) + "</dir>"
    config += (
        "<cachedir>" + escape((args.directory / "font-cache").resolve().as_posix()) + "</cachedir>"
    )
    config += "</fontconfig>"
    (args.directory / "fonts.conf").write_text(config, encoding="utf-8")
    print(target)

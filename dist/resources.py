#!/usr/bin/env python3
"""Download and verify font resources used by portable release builds."""

import argparse
import json
from pathlib import Path
from bootstrap import ROOT, download


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "build/resources")
    parser.add_argument("--manifest", type=Path, default=ROOT / "dist/resources.json")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    lock = json.loads(args.manifest.read_text(encoding="utf-8"))
    for entry in lock["files"]:
        target = args.output / entry["name"]
        target.parent.mkdir(parents=True, exist_ok=True)
        download(entry["url"], entry["sha256"], target)


if __name__ == "__main__":
    main()

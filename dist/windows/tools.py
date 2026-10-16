#!/usr/bin/env python3
"""Install the checksum-verified installer compiler in the build directory."""

import json
from pathlib import Path
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from bootstrap import ROOT, download


def main():
    tools = json.loads((ROOT / "dist/tools.json").read_text())
    cache = ROOT / "build/tool-cache"
    cache.mkdir(parents=True, exist_ok=True)
    item = tools["inno"]
    setup = download(item["url"], item["sha256"], cache / item["name"])
    destination = ROOT / "build/inno"
    subprocess.run(
        [
            str(setup),
            "/VERYSILENT",
            "/SUPPRESSMSGBOXES",
            "/NORESTART",
            "/CURRENTUSER",
            "/DIR=" + str(destination),
        ],
        check=True,
    )
    print(destination / "ISCC.exe")


if __name__ == "__main__":
    main()

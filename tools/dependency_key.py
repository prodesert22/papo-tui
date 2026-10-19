"""Identify the native toolchain and runner image before restoring compiled dependencies."""

import hashlib
import json
import os
import platform
import subprocess


def fingerprint():
    data = {
        "system": platform.system(),
        "architecture": platform.machine(),
        "msystem": os.environ.get("MSYSTEM"),
        "image": os.environ.get("ImageVersion"),
    }
    for name, command in {
        "compiler": ["clang" if os.name == "nt" else "cc", "--version"],
        "host_libraries": ["pkg-config", "--modversion", "glib-2.0", "pangocairo", "fontconfig"],
    }.items():
        result = subprocess.run(command, check=True, capture_output=True)
        data[name] = result.stdout.decode("utf-8", errors="replace").strip()
    return hashlib.sha256(json.dumps(data, sort_keys=True).encode()).hexdigest()


if __name__ == "__main__":
    print("fingerprint=" + fingerprint())

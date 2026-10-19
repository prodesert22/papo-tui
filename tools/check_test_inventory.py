"""Fail on duplicate, missing or stale Python scenario registrations."""

import argparse
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tests/support"))
from run_case import discover  # noqa: E402


def check(build):
    data = json.loads(
        subprocess.check_output(
            ["ctest", "--test-dir", str(build), "-N", "--show-only=json-v1"], text=True
        )
    )
    inventory = {}
    for test in data["tests"]:
        command = test.get("command", [])
        if "--script" not in command:
            continue
        script = Path(command[command.index("--script") + 1]).relative_to(ROOT).as_posix()
        case = command[command.index("--case") + 1]
        key = (script, case)
        if key in inventory:
            raise ValueError(f"Duplicate scenario: {key}")
        inventory[key] = test["name"]
        labels = next((p["value"] for p in test["properties"] if p["name"] == "LABELS"), [])
        if not labels:
            raise ValueError(f"Missing labels: {key}")
    exclusions = json.loads((ROOT / "tests/platforms.json").read_text(encoding="utf-8"))
    for relative, policy in exclusions.items():
        declared = discover(ROOT / relative)
        for platform_cases in policy.get("skips", {}).values():
            stale = set(platform_cases) - set(declared)
            if stale:
                raise ValueError(f"Stale platform skip declarations: {relative}: {sorted(stale)}")
    platform = "windows" if sys.platform == "win32" else "linux"
    expected = set()
    skipped = []
    cache = (Path(build) / "CMakeCache.txt").read_text(encoding="utf-8")
    optional = {
        "test_desktop_notifications.py": "PAPO_DESKTOP_NOTIFICATIONS",
        "test_persistent_sessions.py": "PAPO_DESKTOP_SESSIONS",
    }
    for folder in ("unit", "integration", "terminal"):
        for script in sorted((ROOT / "tests" / folder).glob("test_*.py")):
            relative = script.relative_to(ROOT).as_posix()
            policy = exclusions.get(relative, {})
            reason = policy.get(platform)
            option = optional.get(script.name)
            if option and f"{option}:BOOL=OFF" in cache:
                reason = option + " is explicitly disabled"
            if reason:
                skipped.append({"script": relative, "reason": reason})
                continue
            expected.update((relative, case) for case in discover(script))
    missing = expected - set(inventory)
    stale = set(inventory) - expected
    if missing or stale:
        raise ValueError(
            f"Missing registrations: {sorted(missing)}; stale registrations: {sorted(stale)}"
        )
    report = {"platform": platform, "cases": len(inventory), "excluded": skipped}
    target = Path(build) / "ci-logs"
    target.mkdir(parents=True, exist_ok=True)
    (target / "inventory.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(
        f"Verified {len(inventory)} unique Python scenarios; {len(skipped)} explicit platform/feature exclusions"
    )
    return report


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    check(parser.parse_args().build)

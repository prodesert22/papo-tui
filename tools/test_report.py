"""Summarize CTest JUnit results without treating retries or skips as passes."""

import argparse
from collections import defaultdict
import json
from pathlib import Path
import statistics
import xml.etree.ElementTree as ET


def read_results(path):
    root = ET.parse(path).getroot()
    return [
        {
            "name": case.get("name"),
            "seconds": float(case.get("time", "0")),
            "status": "failed"
            if case.find("failure") is not None or case.find("error") is not None
            else "skipped"
            if case.find("skipped") is not None
            else "passed",
        }
        for case in root.iter("testcase")
    ]


def summarize(path, output, elapsed=None):
    cases = read_results(path)
    budgets = json.loads(
        (Path(__file__).resolve().parents[1] / "tests/time_budgets.json").read_text(
            encoding="utf-8"
        )
    )
    for case in cases:
        if case["name"] in budgets:
            case["timing_reason"] = budgets[case["name"]]["reason"]
    grouped = defaultdict(float)
    for case in cases:
        grouped[case["name"].split("/", 1)[0]] += case["seconds"]
    report = {
        "cases": cases,
        "suite_seconds": dict(sorted(grouped.items())),
        "elapsed_seconds": elapsed,
        "summed_seconds": sum(c["seconds"] for c in cases),
        "over_30_seconds": [c for c in cases if c["seconds"] > 30],
    }
    output = Path(output)
    output.mkdir(parents=True, exist_ok=True)
    (output / "timings.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    lines = ["| Scenario | Seconds | Result |", "| --- | ---: | --- |"]
    lines += [
        f"| {c['name']} | {c['seconds']:.2f} | {c['status']} |"
        for c in sorted(cases, key=lambda c: c["seconds"], reverse=True)[:30]
    ]
    text = (
        f"{len(cases)} CTest cases; {len(report['over_30_seconds'])} above 30 seconds.\n\n"
        + "\n".join(lines)
        + "\n"
    )
    (output / "summary.md").write_text(text, encoding="utf-8")
    print(text)
    return report


def compare(paths):
    rows = [json.loads(Path(p).read_text(encoding="utf-8")) for p in paths]
    if any(case["status"] == "failed" for row in rows for case in row.get("cases", [])):
        raise ValueError(
            "Failed attempts must not be included in successful-run timing comparisons"
        )
    inventories = [{case["name"] for case in row.get("cases", [])} for row in rows]
    if inventories and any(inventory != inventories[0] for inventory in inventories[1:]):
        raise ValueError("Scenario inventories differ; compare equivalent runs")
    elapsed = [row["elapsed_seconds"] for row in rows if row.get("elapsed_seconds") is not None]
    return {
        "runs": len(rows),
        "median_elapsed_seconds": statistics.median(elapsed) if elapsed else None,
        "median_summed_seconds": statistics.median(r["summed_seconds"] for r in rows),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("junit", nargs="?", type=Path)
    parser.add_argument("--output", type=Path, default=Path("build/test-reports"))
    parser.add_argument("--compare", nargs="+", type=Path)
    args = parser.parse_args()
    if args.compare:
        print(json.dumps(compare(args.compare), indent=2))
    elif args.junit:
        summarize(args.junit, args.output)
    else:
        parser.error("provide a JUnit file or --compare timing reports")


if __name__ == "__main__":
    main()

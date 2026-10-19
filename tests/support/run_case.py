"""Discover unittest methods without importing fixtures, or run one explicit method."""

import argparse
import faulthandler
import ast
import importlib.util
import json
import itertools
import os
from pathlib import Path
import re
import sys
import time
import traceback
import unittest


def discover(script):
    tree = ast.parse(Path(script).read_text(encoding="utf-8"))
    cases = []
    for node in tree.body:
        if not isinstance(node, ast.ClassDef):
            continue
        for method in node.body:
            if not isinstance(method, ast.FunctionDef) or not method.name.startswith("test_"):
                continue
            protocols = sorted(
                {
                    arg.value
                    for child in ast.walk(method)
                    if isinstance(child, ast.Call)
                    and isinstance(child.func, ast.Name)
                    and child.func.id == "protocols"
                    for arg in child.args
                    if isinstance(arg, ast.Constant) and isinstance(arg.value, str)
                }
            )
            base = f"{node.name}.{method.name}"
            from test_context import panel_id

            panels = sorted(
                {
                    panel_id(ast.literal_eval(arg)[1])
                    for child in ast.walk(method)
                    if isinstance(child, ast.Call)
                    and isinstance(child.func, ast.Name)
                    and child.func.id == "panels"
                    for arg in child.args
                }
            )
            for protocol, panel in itertools.product(protocols or [None], panels or [None]):
                selectors = [protocol] if protocol else []
                if panel:
                    selectors.append("panel=" + panel)
                cases.append(base + ("[" + ",".join(selectors) + "]" if selectors else ""))
    return sorted(cases)


def redact(value):
    text = str(value)
    for secret in ("Synthetic!123", "NeverPrintThis!", "user-fixture"):
        text = text.replace(secret, "[redacted]")
    return re.sub(
        r"(?i)(Auth=|Authorization:|password[\"']?\s*[:=]\s*)[^\s,;\"']+", r"\1[redacted]", text
    )


class RedactingStream:
    def __init__(self, stream):
        self.stream = stream

    def write(self, value):
        return self.stream.write(redact(value))

    def flush(self):
        self.stream.flush()


def diagnostics(directory, case=None):
    import test_context

    if (directory / "diagnostic.json").exists():
        return
    data = {
        "checkpoint": getattr(case, "_protocol_checkpoint", None),
        "terminals": [
            {
                "screen": getattr(t, "_screen_before_close", t.screen.display),
                "output": redact(repr(bytes(t.output))),
            }
            for t in test_context.terminals
        ],
        "requests": [
            [[request[0], request[1]] for request in server.requests]
            for server in test_context.servers
        ],
    }
    directory.mkdir(parents=True, exist_ok=True)
    (directory / "diagnostic.json").write_text(
        redact(json.dumps(data, ensure_ascii=True, indent=2)), encoding="utf-8"
    )


class CaseResult(unittest.TextTestResult):
    def __init__(self, *args, directory):
        super().__init__(*args)
        self.directory = directory

    def snapshot(self, case):
        try:
            diagnostics(self.directory, case)
        except Exception:
            traceback.print_exc()

    def addFailure(self, case, error):
        self.snapshot(case)
        super().addFailure(case, error)

    def addError(self, case, error):
        self.snapshot(case)
        super().addError(case, error)

    def addSubTest(self, case, subtest, error):
        if error is not None:
            self.snapshot(case)
        super().addSubTest(case, subtest, error)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--discover", type=Path)
    parser.add_argument("--script", type=Path)
    parser.add_argument("--case")
    parser.add_argument("arguments", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    if args.discover:
        print("\n".join(discover(args.discover)))
        return
    if not args.script or not args.case:
        parser.error("--script and --case are required")
    arguments = args.arguments[1:] if args.arguments[:1] == ["--"] else args.arguments
    os.environ["PAPO_TEST_ARGUMENTS"] = json.dumps(arguments)
    os.environ.pop("PAPO_TEST_PROTOCOL", None)
    os.environ.pop("PAPO_TEST_PANEL", None)
    script = args.script.resolve()
    sys.path.insert(0, str(script.parent))
    started = time.monotonic()
    identifier = f"{script.stem}.{args.case}"
    directory = Path(os.environ.get("PAPO_TEST_REPORTS", "Testing/Temporary/cases")) / identifier
    directory.mkdir(parents=True, exist_ok=True)
    (directory / "diagnostic.json").unlink(missing_ok=True)
    faulthandler.dump_traceback_later(45, repeat=True)
    try:
        if args.case not in discover(script):
            raise ValueError("Unknown scenario: " + args.case)
        spec = importlib.util.spec_from_file_location("papo_case_" + script.stem, script)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        selector = args.case
        if selector.endswith("]"):
            selector, protocol = selector.rsplit("[", 1)
            for part in protocol[:-1].split(","):
                if part.startswith("panel="):
                    os.environ["PAPO_TEST_PANEL"] = part.split("=", 1)[1]
                else:
                    os.environ["PAPO_TEST_PROTOCOL"] = part
        suite = unittest.defaultTestLoader.loadTestsFromName(selector, module)
        result = unittest.TextTestRunner(
            verbosity=2,
            stream=RedactingStream(sys.stderr),
            resultclass=lambda *values: CaseResult(*values, directory=directory),
        ).run(suite)
        skipped = result.testsRun > 0 and len(result.skipped) == result.testsRun
        successful = result.wasSuccessful() and result.testsRun > 0
        import test_context

        for terminal in test_context.terminals:
            if not getattr(terminal, "_closed", False) and not terminal.close():
                successful = False
        record = {
            "case": identifier,
            "seconds": time.monotonic() - started,
            "tests": result.testsRun,
            "skipped": [reason for _, reason in result.skipped],
            "success": successful,
        }
    except BaseException:
        traceback.print_exc()
        successful = False
        skipped = False
        record = {"case": identifier, "seconds": time.monotonic() - started, "success": False}
    faulthandler.cancel_dump_traceback_later()
    directory.mkdir(parents=True, exist_ok=True)
    (directory / "result.json").write_text(json.dumps(record, indent=2), encoding="utf-8")
    if not successful:
        diagnostics(directory)
    print(f"CASE {identifier}: {record['seconds']:.3f}s", flush=True)
    raise SystemExit(77 if successful and skipped else 0 if successful else 1)


if __name__ == "__main__":
    main()

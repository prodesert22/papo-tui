"""Choose the CTest scenarios affected by changed files.

A test depends on the files compiled into the executables it runs (from the CMake
File API code model, including headers through #include), on the Python modules
its script imports and on any repository file named in its command. Rules in
tests/areas.json then narrow changes inside self-contained features to the tests
labelled with that feature's areas, mark documentation as needing no tests and
send build-wide inputs to the whole suite. A changed file that no test depends
on and no rule covers also selects the whole suite: an unknown impact is never
treated as no impact.

The selection is a fast local check. The complete suite in CI stays authoritative.
"""

import argparse
import ast
import json
import os
from pathlib import Path, PurePosixPath
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
QUERY = Path(".cmake/api/v1/query/client-papo/codemodel-v2")
PYTHON_PATHS = ("tests/support", "tests/terminal", "tests/integration", "")
INCLUDE = re.compile(rb'^\s*#\s*include\s+"([^"]+)"', re.MULTILINE)
SOURCE_SUFFIXES = {".c", ".h", ".inc"}
ALL = "*"


def glob_regex(pattern):
    """Translate a repository glob: ** spans directories, * and ? stay within one."""
    parts = []
    index = 0
    while index < len(pattern):
        if pattern.startswith("**/", index):
            parts.append("(?:.*/)?")
            index += 3
        elif pattern.startswith("**", index):
            parts.append(".*")
            index += 2
        elif pattern[index] == "*":
            parts.append("[^/]*")
            index += 1
        elif pattern[index] == "?":
            parts.append("[^/]")
            index += 1
        else:
            parts.append(re.escape(pattern[index]))
            index += 1
    return re.compile("".join(parts) + r"\Z")


def load_rules(path=ROOT / "tests/areas.json"):
    data = json.loads(Path(path).read_text(encoding="utf-8"))
    areas = set(data["areas"])
    rules = []
    for rule in data.get("paths", []):
        value = rule["areas"]
        if value != ALL:
            unknown = set(value) - areas
            if unknown:
                raise ValueError(f"{rule['glob']}: unknown areas {sorted(unknown)}")
        rules.append((rule["glob"], glob_regex(rule["glob"]), value))
    return areas, rules


def rule_for(path, rules):
    """First rule whose glob matches path, as (glob, areas), or None."""
    for pattern, regex, value in rules:
        if regex.match(path):
            return pattern, value
    return None


def relative(path, root):
    try:
        return PurePosixPath(Path(path).resolve().relative_to(root.resolve()).as_posix())
    except (ValueError, OSError):
        return None


def include_dependents(root, include_dirs):
    """Map each source/header to the files that include it, directly or not."""
    direct = {}
    for folder in ("src", "tests"):
        for file in (root / folder).rglob("*"):
            if file.suffix not in SOURCE_SUFFIXES or not file.is_file():
                continue
            includer = file.relative_to(root).as_posix()
            for name in INCLUDE.findall(file.read_bytes()):
                name = name.decode("utf-8", "replace")
                for base in (file.parent, *include_dirs):
                    candidate = Path(base, name)
                    if candidate.is_file():
                        target = relative(candidate, root)
                        if target:
                            direct.setdefault(str(target), set()).add(includer)
                        break
    closure = {}
    for header in direct:
        seen, pending = set(), [header]
        while pending:
            for includer in direct.get(pending.pop(), ()):
                if includer not in seen:
                    seen.add(includer)
                    pending.append(includer)
        closure[header] = seen
    return closure


def python_imports(script, root):
    """Repository Python files a script loads through static imports."""
    search = [script.parent] + [root / folder for folder in PYTHON_PATHS]
    seen, pending = set(), [script]
    while pending:
        file = pending.pop()
        if file in seen:
            continue
        seen.add(file)
        try:
            tree = ast.parse(file.read_text(encoding="utf-8"), str(file))
        except (OSError, SyntaxError, UnicodeDecodeError):
            continue
        names = []
        for node in ast.walk(tree):
            if isinstance(node, ast.Import):
                names += [alias.name for alias in node.names]
            elif isinstance(node, ast.ImportFrom) and node.module and not node.level:
                names.append(node.module)
                names += [node.module + "." + alias.name for alias in node.names]
        for name in names:
            path = Path(*name.split("."))
            for base in search:
                for candidate in (base / path.with_suffix(".py"), base / path / "__init__.py"):
                    if candidate.is_file():
                        pending.append(candidate.resolve())
                        break
    return {str(relative(file, root)) for file in seen if relative(file, root)}


def code_model(build, root):
    """Executable/library artifacts mapped to every repository source they contain."""
    reply = Path(build) / ".cmake/api/v1/reply"
    indexes = sorted(reply.glob("index-*.json"))
    if not indexes:
        (Path(build) / QUERY).parent.mkdir(parents=True, exist_ok=True)
        (Path(build) / QUERY).touch()
        subprocess.run(["cmake", str(build)], check=True, stdout=subprocess.DEVNULL)
        indexes = sorted(reply.glob("index-*.json"))
    index = json.loads(indexes[-1].read_text(encoding="utf-8"))
    entry = index["reply"]["client-papo"]["codemodel-v2"]
    model = json.loads((reply / entry["jsonFile"]).read_text(encoding="utf-8"))
    source = Path(model["paths"]["source"])
    targets = {}
    include_dirs = set()
    for summary in model["configurations"][0]["targets"]:
        target = json.loads((reply / summary["jsonFile"]).read_text(encoding="utf-8"))
        sources = set()
        for item in target.get("sources", []):
            path = relative(source / item["path"], root)
            if path:
                sources.add(str(path))
        for group in target.get("compileGroups", []):
            for include in group.get("includes", []):
                if relative(include["path"], root) is not None:
                    include_dirs.add(include["path"])
        artifacts = {
            str((Path(model["paths"]["build"]) / item["path"]).resolve())
            for item in target.get("artifacts", [])
        }
        targets[target["id"]] = {
            "sources": sources,
            "artifacts": artifacts,
            "dependencies": {item["id"] for item in target.get("dependencies", [])},
        }
    by_artifact = {}
    for identifier, target in targets.items():
        seen, pending, files = set(), [identifier], set()
        while pending:
            current = pending.pop()
            if current in seen or current not in targets:
                continue
            seen.add(current)
            files |= targets[current]["sources"]
            pending += targets[current]["dependencies"]
        for artifact in target["artifacts"]:
            by_artifact[artifact] = files
    return by_artifact, sorted(include_dirs)


def test_index(build, root=ROOT):
    """CTest name -> (repository files it depends on, its labels)."""
    listing = json.loads(
        subprocess.check_output(
            ["ctest", "--test-dir", str(build), "-N", "--show-only=json-v1"], text=True
        )
    )
    artifacts, include_dirs = code_model(build, root)
    cache = {}

    def dependencies(piece, directory, folders):
        """Repository files that one command or environment value refers to. Environment
        search paths name whole folders, so only command arguments count folders."""
        key = (piece, directory, folders)
        if key in cache:
            return cache[key]
        files = frozenset()
        path = Path(piece) if Path(piece).is_absolute() else directory / piece
        if not path.exists() and not Path(piece).is_absolute():
            path = root / piece
        if path.exists():
            resolved = str(path.resolve())
            inside = relative(path, root)
            if resolved in artifacts:
                files = frozenset(artifacts[resolved])
            elif inside is not None and str(inside) not in ("", "."):
                if path.is_dir() and not folders:
                    pass
                elif path.is_dir():
                    files = frozenset(
                        str(relative(item, root)) for item in path.rglob("*") if item.is_file()
                    )
                elif path.suffix == ".py":
                    files = frozenset({str(inside)} | python_imports(path.resolve(), root))
                else:
                    files = frozenset({str(inside)})
        cache[key] = files
        return files

    separators = re.compile(r"[=:;]" if os.name != "nt" else r"[=;]")
    tests = {}
    for test in listing["tests"]:
        properties = {item["name"]: item["value"] for item in test.get("properties", [])}
        values = [(value, True) for value in test.get("command", [])]
        values += [(value.split("=", 1)[-1], False) for value in properties.get("ENVIRONMENT", [])]
        directory = Path(properties.get("WORKING_DIRECTORY", build))
        files = set()
        for value, folders in values:
            for piece in separators.split(value):
                if piece:
                    files |= dependencies(piece, directory, folders)
        tests[test["name"]] = (files, set(properties.get("LABELS", [])))
    return tests, include_dependents(root, include_dirs)


def select(changed, tests, dependents, rules):
    """Return (selected names or ALL, [(path, explanation)])."""
    selected = set()
    reasons = []
    for path in changed:
        affected = {path} | dependents.get(path, set())
        reachable = {name for name, (files, _) in tests.items() if files & affected}
        rule = rule_for(path, rules)
        if rule and rule[1] == ALL:
            reasons.append((path, f"rule {rule[0]} runs the whole suite"))
            return ALL, reasons
        if rule and not rule[1]:
            reasons.append((path, f"rule {rule[0]}: no tests"))
            continue
        if rule:
            labels = {"area:" + area for area in rule[1]}
            in_area = {name for name, (_, tags) in tests.items() if tags & labels}
            chosen = (reachable & in_area) or in_area
            how = "tests that use it" if reachable & in_area else "no test uses it directly"
            reasons.append(
                (path, f"{len(chosen)} tests in {', '.join(sorted(rule[1]))} ({how}; {rule[0]})")
            )
            selected |= chosen
        elif reachable:
            reasons.append((path, f"{len(reachable)} tests that use it"))
            selected |= reachable
        else:
            reasons.append((path, "no test or rule covers it: whole suite"))
            return ALL, reasons
    return selected, reasons


def changed_files(base, root=ROOT):
    """Files changed since the merge base with base, including uncommitted and untracked ones."""
    merge_base = subprocess.check_output(
        ["git", "merge-base", base, "HEAD"], cwd=root, text=True
    ).strip()
    tracked = subprocess.check_output(
        ["git", "diff", "--name-only", "--no-renames", merge_base], cwd=root, text=True
    ).split()
    untracked = subprocess.check_output(
        ["git", "ls-files", "--others", "--exclude-standard"], cwd=root, text=True
    ).split()
    return sorted(set(tracked) | set(untracked))


def choose(build, base, root=ROOT, files=None):
    _, rules = load_rules(root / "tests/areas.json")
    tests, dependents = test_index(build, root)
    changed = sorted(files) if files is not None else changed_files(base, root)
    selected, reasons = select(changed, tests, dependents, rules)
    return selected, reasons, tests


def report(selected, reasons, tests, stream=sys.stdout):
    for path, why in reasons:
        print(f"  {path}: {why}", file=stream)
    if selected == ALL:
        print(f"Selected the whole suite ({len(tests)} tests)", file=stream)
    else:
        print(f"Selected {len(selected)} of {len(tests)} tests", file=stream)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--base", default="dev", help="branch or commit to compare against")
    parser.add_argument("--list", action="store_true", help="print the selected test names")
    parser.add_argument(
        "--files", nargs="+", metavar="PATH", help="evaluate these repository paths instead of git"
    )
    args = parser.parse_args()
    selected, reasons, tests = choose(args.build.resolve(), args.base, files=args.files)
    report(selected, reasons, tests, sys.stderr)
    if args.list:
        for name in sorted(tests if selected == ALL else selected):
            print(name)


if __name__ == "__main__":
    main()

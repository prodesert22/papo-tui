"""Keep po/en.po and the compiled-in English catalogue in step with the marked source text.

Without options, extract the strings marked with _(), C_(), P_(), N_() and NC_() from
src/, merge them into po/en.po (keeping existing translations, adding new entries with
an empty translation and dropping entries no longer in the source) and regenerate
src/model/text/translation_catalog.inc. --check changes nothing and fails when the
catalogue is out of date, a translation is missing or marked fuzzy, or a translation's
printf conversions differ from its source.

The runtime catalogue is compiled in instead of loaded through libintl: the language
then switches immediately from the settings panel, whatever the system locale (gettext
ignores LANGUAGE under the C locale), and no package format has to install and locate
.mo files. The .po subset written here (comments, msgctxt, msgid, msgid_plural, msgstr
and msgstr[n]) stays readable by standard gettext editors.
"""

import argparse
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
CATALOGUE = ROOT / "po/en.po"
GENERATED = ROOT / "src/model/text/translation_catalog.inc"
MARKERS = {"_": (None, 1), "N_": (None, 1), "C_": ("context", 2), "NC_": ("context", 2)}
PLURAL = {"P_"}
CONVERSION = re.compile(
    r"%(?:%|[-+ #0']*(?:\*|\d+)?(?:\.(?:\*|\d+))?(?:hh|h|ll|l|z|j|t|L)?[a-zA-Z])"
)
HEADER = (
    'msgid ""\n'
    'msgstr ""\n'
    '"Project-Id-Version: papo-tui\\n"\n'
    '"Language: en\\n"\n'
    '"MIME-Version: 1.0\\n"\n'
    '"Content-Type: text/plain; charset=UTF-8\\n"\n'
    '"Content-Transfer-Encoding: 8bit\\n"\n'
    '"Plural-Forms: nplurals=2; plural=(n != 1);\\n"\n'
)


def c_unescape(body):
    """Bytes of a C string literal body; source files are UTF-8."""
    out = bytearray()
    raw = body.encode("utf-8")
    index = 0
    simple = {
        b"n": b"\n",
        b"t": b"\t",
        b"r": b"\r",
        b"\\": b"\\",
        b'"': b'"',
        b"'": b"'",
        b"a": b"\a",
        b"b": b"\b",
        b"f": b"\f",
        b"v": b"\v",
        b"?": b"?",
        b"e": b"\x1b",
    }
    while index < len(raw):
        char = raw[index : index + 1]
        if char != b"\\":
            out += char
            index += 1
            continue
        following = raw[index + 1 : index + 2]
        if following in simple:
            out += simple[following]
            index += 2
        elif following == b"x":
            digits = re.match(rb"[0-9a-fA-F]+", raw[index + 2 :]).group()
            out.append(int(digits, 16) & 0xFF)
            index += 2 + len(digits)
        elif following.isdigit():
            digits = re.match(rb"[0-7]{1,3}", raw[index + 1 :]).group()
            out.append(int(digits, 8))
            index += 1 + len(digits)
        else:
            raise ValueError(f"unsupported escape \\{following.decode()}")
    return out.decode("utf-8")


def tokens(text):
    """(kind, value, line) for identifiers, string literals and punctuation, skipping
    comments, character literals and preprocessor directives."""
    pattern = re.compile(
        r'(?P<comment>//[^\n]*|/\*.*?\*/)|(?P<string>"(?:[^"\\\n]|\\.)*")|'
        r"(?P<char>'(?:[^'\\\n]|\\.)*')|(?P<directive>^[ \t]*#(?:[^\n]*\\\n)*[^\n]*)|"
        r"(?P<name>[A-Za-z_]\w*)|(?P<space>\s+)|(?P<other>.)",
        re.DOTALL | re.MULTILINE,
    )
    line = 1
    for match in pattern.finditer(text):
        kind = match.lastgroup
        value = match.group()
        if kind in ("string", "name", "other"):
            yield kind, value, line
        line += value.count("\n")


def literal(stream, index):
    """Concatenated string literal starting at stream[index]: (text, next index) or None."""
    parts = []
    while index < len(stream) and stream[index][0] == "string":
        parts.append(c_unescape(stream[index][1][1:-1]))
        index += 1
    return ("".join(parts), index) if parts else None


def extract(root=ROOT):
    """Marked strings: {(context, msgid): {"plural": str|None, "places": [..]}}."""
    entries = {}
    for path in sorted((root / "src").rglob("*")):
        if path.suffix not in (".c", ".h") or not path.is_file():
            continue
        relative = path.relative_to(root).as_posix()
        stream = list(tokens(path.read_text(encoding="utf-8")))
        for index, (kind, value, line) in enumerate(stream):
            if kind != "name" or (value not in MARKERS and value not in PLURAL):
                continue
            if index + 1 >= len(stream) or stream[index + 1][1] != "(":
                continue
            arguments = []
            position = index + 2
            count = 3 if value in PLURAL else MARKERS[value][1]
            for argument in range(count):
                found = literal(stream, position)
                if not found:
                    break
                arguments.append(found[0])
                position = found[1]
                if argument + 1 < count:
                    if position >= len(stream) or stream[position][1] != ",":
                        break
                    position += 1
            if value in PLURAL:
                if len(arguments) < 2:
                    continue
                context, msgid, plural = "", arguments[0], arguments[1]
            else:
                if len(arguments) < count:
                    continue
                context = arguments[0] if count == 2 else ""
                msgid, plural = arguments[-1], None
            if not msgid:
                raise ValueError(f"{relative}:{line}: empty marked string")
            entry = entries.setdefault((context, msgid), {"plural": plural, "places": []})
            if entry["plural"] != plural:
                raise ValueError(f"{relative}:{line}: {msgid!r} used with different plurals")
            # Files only, as xgettext --add-location=file: edits elsewhere leave the
            # catalogue unchanged.
            if relative not in entry["places"]:
                entry["places"].append(relative)
    return entries


def po_quote(text):
    escaped = (
        text.replace("\\", "\\\\").replace('"', '\\"').replace("\t", "\\t").replace("\r", "\\r")
    )
    lines = escaped.split("\n")
    pieces = [line + "\\n" for line in lines[:-1]] + ([lines[-1]] if lines[-1] else [])
    if len(pieces) <= 1:
        return '"' + (pieces[0] if pieces else "") + '"'
    return '""\n' + "\n".join('"' + piece + '"' for piece in pieces)


def po_unquote(chunks):
    text = "".join(chunks)
    out = []
    index = 0
    while index < len(text):
        if text[index] == "\\" and index + 1 < len(text):
            out.append({"n": "\n", "t": "\t", "r": "\r", '"': '"', "\\": "\\"}[text[index + 1]])
            index += 2
        else:
            out.append(text[index])
            index += 1
    return "".join(out)


def read_po(path=CATALOGUE):
    """{(context, msgid): {"plural", "translation": [..], "fuzzy"}} without the header."""
    entries = {}
    if not Path(path).is_file():
        return entries
    blocks = Path(path).read_text(encoding="utf-8").split("\n\n")
    for block in blocks:
        fields, current, fuzzy = {}, None, False
        for line in block.splitlines():
            if line.startswith("#,") and "fuzzy" in line:
                fuzzy = True
            if not line or line.startswith("#"):
                continue
            match = re.match(r'(msgctxt|msgid_plural|msgid|msgstr(?:\[\d+\])?)\s+(".*")$', line)
            if match:
                current = match.group(1)
                fields[current] = [match.group(2)[1:-1]]
            elif line.startswith('"') and current:
                fields[current].append(line[1:-1])
            else:
                raise ValueError(f"{path}: unexpected line {line!r}")
        if "msgid" not in fields:
            continue
        msgid = po_unquote(fields["msgid"])
        if not msgid:
            continue
        context = po_unquote(fields.get("msgctxt", [""]))
        plural = po_unquote(fields["msgid_plural"]) if "msgid_plural" in fields else None
        if plural is None:
            translation = [po_unquote(fields.get("msgstr", [""]))]
        else:
            translation = [po_unquote(fields.get(f"msgstr[{n}]", [""])) for n in (0, 1)]
        entries[(context, msgid)] = {"plural": plural, "translation": translation, "fuzzy": fuzzy}
    return entries


def key_order(key):
    return (key[0].encode("utf-8"), key[1].encode("utf-8"))


def write_po(source, existing):
    out = [HEADER]
    for key in sorted(source, key=key_order):
        context, msgid = key
        entry = source[key]
        known = existing.get(key)
        if known and known["plural"] != entry["plural"]:
            known = None
        translation = known["translation"] if known else [""] * (2 if entry["plural"] else 1)
        lines = ["#: " + " ".join(entry["places"])]
        if known and known["fuzzy"]:
            lines.append("#, fuzzy")
        if context:
            lines.append("msgctxt " + po_quote(context))
        lines.append("msgid " + po_quote(msgid))
        if entry["plural"] is None:
            lines.append("msgstr " + po_quote(translation[0]))
        else:
            lines.append("msgid_plural " + po_quote(entry["plural"]))
            lines += [f"msgstr[{n}] " + po_quote(translation[n]) for n in (0, 1)]
        out.append("\n".join(lines) + "\n")
    return "\n".join(out)


def c_quote(text):
    """C string literal of text; UTF-8 stays as is, controls become octal escapes."""
    out = []
    for char in text:
        if char in '"\\':
            out.append("\\" + char)
        elif char == "\n":
            out.append("\\n")
        elif char == "\t":
            out.append("\\t")
        elif char == "?":
            out.append("\\?")  # Never forms a trigraph.
        elif ord(char) < 0x20 or ord(char) == 0x7F:
            out.append(f"\\{ord(char):03o}")
        else:
            out.append(char)
    return '"' + "".join(out) + '"'


def generate(catalogue):
    lines = [
        "/* Generated by tools/translations.py from po/en.po; do not edit. */",
        "static const translation_entry translation_entries[] = {",
    ]
    keys = sorted(catalogue, key=key_order)
    for key in keys:
        entry = catalogue[key]
        if entry["plural"] is None:
            plural, forms = "NULL", f"{{{c_quote(entry['translation'][0])}, NULL}}"
        else:
            plural = c_quote(entry["plural"])
            forms = "{" + ", ".join(c_quote(form) for form in entry["translation"]) + "}"
        lines.append(f"    {{{c_quote(key[0])}, {c_quote(key[1])}, {plural}, {forms}}},")
    if not keys:
        lines.append('    {"", "", NULL, {"", ""}},')
    lines.append("};")
    return "\n".join(lines) + "\n"


def conversions(text):
    """printf conversions in order; a literal %% consumes no argument."""
    return [found for found in CONVERSION.findall(text) if found != "%%"]


def problems(source, catalogue):
    found = []
    missing = sorted(set(source) - set(catalogue), key=key_order)
    stale = sorted(set(catalogue) - set(source), key=key_order)
    found += [f"not in po/en.po: {key[1]!r}" for key in missing]
    found += [f"no longer in the source: {key[1]!r}" for key in stale]
    for key, entry in sorted(catalogue.items(), key=lambda item: key_order(item[0])):
        if key not in source:
            continue
        if entry["fuzzy"]:
            found.append(f"fuzzy: {key[1]!r}")
        originals = [key[1]] if entry["plural"] is None else [key[1], entry["plural"]]
        for index, translation in enumerate(entry["translation"]):
            if not translation:
                found.append(f"untranslated: {key[1]!r}")
                break
            original = originals[min(index, len(originals) - 1)]
            if conversions(original) != conversions(translation):
                found.append(f"printf conversions differ: {key[1]!r} -> {translation!r}")
    return found


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--check", action="store_true", help="verify without writing")
    args = parser.parse_args()
    source = extract()
    existing = read_po()
    if args.check:
        found = problems(source, existing)
        expected = generate({key: existing[key] for key in source if key in existing})
        actual = GENERATED.read_text(encoding="utf-8") if GENERATED.is_file() else ""
        if not found and expected != actual:
            found.append(f"{GENERATED.relative_to(ROOT)} is out of date")
        for problem in found:
            print(problem, file=sys.stderr)
        if found:
            print("Run python tools/translations.py and translate po/en.po.", file=sys.stderr)
            raise SystemExit(1)
        print(f"{len(source)} translated strings are up to date")
        return
    CATALOGUE.parent.mkdir(parents=True, exist_ok=True)
    CATALOGUE.write_text(write_po(source, existing), encoding="utf-8")
    merged = read_po()
    complete = {key: entry for key, entry in merged.items() if all(entry["translation"])}
    GENERATED.write_text(generate(complete), encoding="utf-8")
    pending = len(source) - len(complete)
    print(f"{len(source)} marked strings; {pending} still need an English translation")


if __name__ == "__main__":
    main()

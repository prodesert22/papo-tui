"""Explicit resources shared by test entrypoints; importing never consumes argv."""

import json
import os
import sys
import unittest


terminals = []
servers = []


def argument(index):
    values = json.loads(os.environ.get("PAPO_TEST_ARGUMENTS", "[]"))
    if not values and __name__ != "__main__":
        values = sys.argv[1:]
    return values[index] if index < len(values) else "unconfigured-test-resource"


def main(count=1, **kwargs):
    unittest.main(argv=[sys.argv[0], *sys.argv[count + 1 :]], **kwargs)


def protocols(*values):
    """Select one advertised graphics protocol; direct execution covers all."""
    selected = os.environ.get("PAPO_TEST_PROTOCOL")
    return [value for value in values if not selected or value == selected]


def panel_id(title):
    import re
    import unicodedata

    text = unicodedata.normalize("NFKD", title).encode("ascii", "ignore").decode()
    return re.sub(r"[^a-z0-9]+", "-", text.lower()).strip("-")


def panels(*values):
    selected = os.environ.get("PAPO_TEST_PANEL")
    return [value for value in values if not selected or panel_id(value[1]) == selected]

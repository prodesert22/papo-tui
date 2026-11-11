"""Extraction, .po round trips and checks of tools/translations.py."""

from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import translations  # noqa: E402

SOURCE = r"""
#define _(text) translate(NULL, (text))
/* _("comentário") is not text */
// _("linha comentada")
static const char *const labels[] = {N_("Escuro"), N_("Claro")};
void draw(int n)
{
    printf(_("Abrir " "conversa\n"));
    printf(C_("botão", "Abrir"));
    printf(P_("%zu mensagem", "%zu mensagens", n), (size_t)n);
    printf(_("Aspas \"internas\" e \\ barra"));
    char quote = '"';
    printf(_("Depois do caractere"));
    printf(_(variable));
}
"""


def extract(text):
    with tempfile.TemporaryDirectory() as folder:
        root = Path(folder)
        (root / "src").mkdir()
        (root / "src/a.c").write_text(text, encoding="utf-8")
        return translations.extract(root)


class TranslationTests(unittest.TestCase):
    def test_extraction_reads_markers_and_skips_comments_macros_and_variables(self):
        found = extract(SOURCE)
        self.assertEqual(
            set(found),
            {
                ("", "Escuro"),
                ("", "Claro"),
                ("", "Abrir conversa\n"),
                ("botão", "Abrir"),
                ("", "%zu mensagem"),
                ("", 'Aspas "internas" e \\ barra'),
                ("", "Depois do caractere"),
            },
        )
        self.assertEqual(found[("", "%zu mensagem")]["plural"], "%zu mensagens")
        self.assertEqual(found[("", "Escuro")]["places"], ["src/a.c"])

    def test_po_round_trip_keeps_translations_contexts_and_plurals(self):
        found = extract(SOURCE)
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder, "en.po")
            path.write_text(translations.write_po(found, {}), encoding="utf-8")
            empty = translations.read_po(path)
            self.assertEqual(set(empty), set(found))
            self.assertEqual(empty[("", "%zu mensagem")]["translation"], ["", ""])
            existing = {
                ("botão", "Abrir"): {"plural": None, "translation": ["Open"], "fuzzy": False},
                ("", "Abrir conversa\n"): {
                    "plural": None,
                    "translation": ["Open conversation\n"],
                    "fuzzy": False,
                },
                ("", "%zu mensagem"): {
                    "plural": "%zu mensagens",
                    "translation": ["%zu message", "%zu messages"],
                    "fuzzy": True,
                },
                ("", "Removido"): {"plural": None, "translation": ["Removed"], "fuzzy": False},
            }
            path.write_text(translations.write_po(found, existing), encoding="utf-8")
            merged = translations.read_po(path)
        self.assertNotIn(("", "Removido"), merged)
        self.assertEqual(merged[("botão", "Abrir")]["translation"], ["Open"])
        self.assertEqual(merged[("", "Abrir conversa\n")]["translation"], ["Open conversation\n"])
        self.assertTrue(merged[("", "%zu mensagem")]["fuzzy"])
        problems = translations.problems(found, merged)
        self.assertIn("fuzzy: '%zu mensagem'", problems)
        self.assertIn("untranslated: 'Escuro'", problems)

    def test_conversions_must_match_in_order(self):
        source = {("", "%s de %zu"): {"plural": None, "places": []}}
        for translation, valid in (
            ("%s of %zu", True),
            ("%zu of %s", False),
            ("%s of", False),
            ("100%% %s of %zu", True),
        ):
            with self.subTest(translation=translation):
                catalogue = {
                    ("", "%s de %zu"): {
                        "plural": None,
                        "translation": [translation],
                        "fuzzy": False,
                    }
                }
                self.assertEqual(not translations.problems(source, catalogue), valid)

    def test_missing_and_stale_entries_are_reported(self):
        source = {("", "Novo"): {"plural": None, "places": []}}
        catalogue = {("", "Velho"): {"plural": None, "translation": ["Old"], "fuzzy": False}}
        problems = translations.problems(source, catalogue)
        self.assertIn("not in po/en.po: 'Novo'", problems)
        self.assertIn("no longer in the source: 'Velho'", problems)

    def test_generated_catalogue_is_sorted_and_escaped_for_c(self):
        catalogue = {
            ("", "b?"): {"plural": None, "translation": ['"b"\n'], "fuzzy": False},
            ("", "a"): {
                "plural": "as",
                "translation": ["one", "many"],
                "fuzzy": False,
            },
        }
        generated = translations.generate(catalogue)
        self.assertLess(generated.index('"a"'), generated.index('"b\\?"'))
        self.assertIn('{"", "b\\?", NULL, {"\\"b\\"\\n", NULL}},', generated)
        self.assertIn('{"", "a", "as", {"one", "many"}},', generated)
        self.assertIn('{"", "", NULL, {"", ""}}', translations.generate({}))

    def test_repository_catalogue_is_current(self):
        source = translations.extract()
        catalogue = translations.read_po()
        self.assertEqual(translations.problems(source, catalogue), [])


if __name__ == "__main__":
    unittest.main()

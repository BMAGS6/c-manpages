"""Regression tests for NAME-driven, offline documentation homepage cards."""
import sys
from pathlib import Path
from tempfile import TemporaryDirectory
from types import SimpleNamespace
import unittest

TOOLS = Path(__file__).resolve().parents[1] / "tools"
sys.path.insert(0, str(TOOLS))
from build_pages import BuildError, read_name_summary  # noqa: E402
from site_navigation import render_index_categories  # noqa: E402


class HomepageTests(unittest.TestCase):
    def test_name_summary_comes_from_markdown(self):
        with TemporaryDirectory() as directory:
            source = Path(directory) / "example.md"
            source.write_text(
                "title: c-example\nsection: 7\nsource: C Manual Pages\n\n"
                "# NAME\n\n"
                "c-example -- the `example` keyword and\n"
                "its storage rules\n\n"
                "# DESCRIPTION\n\nOther text.\n",
                encoding="utf-8",
            )
            page = SimpleNamespace(name="c-example", source=source)
            self.assertEqual(
                read_name_summary(page), "the example keyword and its storage rules"
            )

    def test_malformed_name_fails(self):
        with TemporaryDirectory() as directory:
            source = Path(directory) / "example.md"
            source.write_text("# NAME\n\nwrong -- description\n", encoding="utf-8")
            page = SimpleNamespace(name="c-example", source=source)
            with self.assertRaises(BuildError):
                read_name_summary(page)

    def test_index_cards_escape_text(self):
        catalog = [{"name": "c-example", "section": "7", "category": "concepts"}]
        rendered = render_index_categories(catalog, {"c-example": "Use <tag> & symbols"})
        self.assertIn('href="c-example.7.html"', rendered)
        self.assertIn("Use &lt;tag&gt; &amp; symbols", rendered)
        self.assertNotIn("Use <tag>", rendered)
        self.assertIn("<h3", rendered)


if __name__ == "__main__":
    unittest.main()

"""Check Phase C navigation, offline HTML structure and safe link rendering.

These tests require only Python's standard library and the project's existing
source files. They intentionally do not call lowdown or a web server: site
navigation is derived from the already-validated Python page registry.
"""

from html.parser import HTMLParser
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

from build_html import DocumentParser, make_toc  # noqa: E402
from site_navigation import make_catalog, render_navigation, render_index_categories  # noqa: E402


class MarkupInspector(HTMLParser):
    """Record navigation links, heading levels and duplicate document IDs."""

    def __init__(self):
        super().__init__()
        self.headings = []
        self.links = []
        self.ids = []

    def handle_starttag(self, tag, attrs):
        attrs = dict(attrs)
        if "id" in attrs:
            self.ids.append(attrs["id"])
        if tag in ("h1", "h2", "h3", "h4", "h5", "h6"):
            self.headings.append(int(tag[1]))
        if tag == "a":
            self.links.append(attrs)


class SiteNavigationTests(unittest.TestCase):
    """Verify that additions to pages/ automatically appear everywhere."""

    def make_catalog(self):
        from dataclasses import dataclass
        from tempfile import TemporaryDirectory

        @dataclass(frozen=True)
        class PageStub:
            source: Path
            name: str
            section: str

        directory = TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        root = Path(directory.name) / "pages"
        pages = [
            PageStub(root / "keywords" / "static.md", "c-static", "7"),
            PageStub(root / "concepts" / "storage-duration.md", "c-storage-duration", "7"),
            PageStub(root / "operators" / "sizeof.md", "c-sizeof", "7"),
        ]
        return make_catalog(pages, root)

    def test_categories_and_links_are_derived_from_page_paths(self):
        """Unknown future categories need no registration or HTML changes."""
        catalog = self.make_catalog()
        markup = render_index_categories(catalog)
        self.assertIn("Keywords", markup)
        self.assertIn("Concepts", markup)
        self.assertIn("Operators", markup)
        self.assertIn('href="c-sizeof.7.html"', markup)
        self.assertIn('href="c-storage-duration.7.html"', markup)

    def test_current_page_indicator_is_on_exactly_one_sidebar_link(self):
        """aria-current identifies the selected document, not all category items."""
        markup = render_navigation(self.make_catalog(), current_page="c-static")
        parser = MarkupInspector()
        parser.feed(markup)
        selected = [link for link in parser.links if link.get("aria-current") == "page"]
        self.assertEqual([link["href"] for link in selected], ["c-static.7.html"])
        self.assertEqual(len(parser.ids), len(set(parser.ids)))
        self.assertIn('href="index.html"', markup)

    def test_home_is_current_only_on_homepage(self):
        """The homepage remains a reachable navigation destination."""
        markup = render_navigation(self.make_catalog())
        self.assertIn('href="index.html" aria-current="page"', markup)
        self.assertNotIn('href="c-static.7.html" aria-current="page"', markup)

    def test_heading_promotion_preserves_fragment_ids_and_toc(self):
        """One outer h1 owns the title; lowdown h1/h2 become h2/h3."""
        original = (
            '<html><head><title>c-static</title></head><body>'
            '<h1 id="description">DESCRIPTION</h1>'
            '<p>Examples: &lt;h1&gt; remains plain text.</p>'
            '<h2 id="semantics">SEMANTICS</h2>'
            '</body></html>'
        )
        document = DocumentParser()
        document.feed(original)
        document.close()
        body = "".join(document.body_parts)
        self.assertIn('<h2 id="description">DESCRIPTION</h2>', body)
        self.assertIn('<h3 id="semantics">SEMANTICS</h3>', body)
        self.assertNotIn('<h1 id="description">', body)
        self.assertIn('&lt;h1&gt;', body)
        self.assertIn('href="#description"', make_toc(document.headings))
        self.assertIn('href="#semantics"', make_toc(document.headings))


if __name__ == "__main__":
    unittest.main()

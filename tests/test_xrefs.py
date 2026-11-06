"""Focused cross-reference tests using the actual .Lk format from lowdown.

Unlike end-to-end tests that substitute lowdown, these verify the precise
output shape observed on Arch Linux: a standalone ``.Lk URL label`` mdoc macro
and a normal HTML ``<a href=...>`` element. This catches accidental changes to
our transformation semantics without depending on lowdown being installed in
this test environment.
"""

from pathlib import Path
import sys
import tempfile
import unittest
from types import SimpleNamespace


PROJECT_ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(PROJECT_ROOT / "tools"))
from xrefs import ReferenceError, ReferenceResolver, rewrite_html, rewrite_mdoc  # noqa: E402


class ReferenceTests(unittest.TestCase):
    """Exercise two registered pages with realistic HTML and mdoc fixtures."""

    def setUp(self):
        """Create real file paths to test relative-link resolution safely."""
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        root = Path(temporary.name)
        static = root / "pages" / "keywords" / "static.md"
        duration = root / "pages" / "concepts" / "storage-duration.md"
        for source in (static, duration):
            source.parent.mkdir(parents=True, exist_ok=True)
            source.write_text("# NAME\n", encoding="utf-8")

        self.static = SimpleNamespace(
            source=static,
            name="c-static",
            section="7",
            html_path=root / "build" / "html" / "c-static.7.html",
        )
        self.duration = SimpleNamespace(
            source=duration,
            name="c-storage-duration",
            section="7",
            html_path=root / "build" / "html" / "c-storage-duration.7.html",
        )
        self.duration_html = (
            '<html><body><h1 id="name">NAME</h1>'
            '<h2 id="static-storage-duration">Static storage duration</h2>'
            '</body></html>'
        )
        self.static_html = '<html><body><h1 id="name">NAME</h1></body></html>'
        self.resolver = ReferenceResolver(
            [self.static, self.duration],
            lambda page: (self.static_html if page.name == "c-static"
                          else self.duration_html),
        )

    def test_probe_mdoc_internal_and_external_macros(self):
        """Turn the observed internal lowdown .Lk into a real .Xr macro."""
        original = (
            '.Sh DESCRIPTION\nFor more information, see\n'
            '.Lk ../concepts/storage-duration.md storage duration\n'
            '\\&.\n.Pp\n'
            '.Lk https://example.org external reference\n'
        )
        transformed, targets = rewrite_mdoc(original, self.resolver, self.static)
        self.assertIn('.Xr c-storage-duration 7\n', transformed)
        self.assertIn('\\&.\n', transformed)
        self.assertIn('.Lk https://example.org external reference\n', transformed)
        self.assertEqual(targets, {self.duration.source})

    def test_html_rewriter_only_changes_real_anchor_elements(self):
        """Leave code literals and HTML entities alone while updating href."""
        original = (
            '<!DOCTYPE html><html><body>'
            '<p><a href="../concepts/storage-duration.md">storage &amp; duration</a></p>'
            '<pre><code>&lt;a href="../concepts/storage-duration.md"&gt;</code></pre>'
            '<p><a href="https://example.org/?x=1&amp;y=2">external</a></p>'
            '</body></html>'
        )
        transformed, targets = rewrite_html(original, self.resolver, self.static)
        self.assertIn('<a href="c-storage-duration.7.html">storage &amp; duration</a>', transformed)
        self.assertIn('&lt;a href="../concepts/storage-duration.md"&gt;', transformed)
        self.assertIn('href="https://example.org/?x=1&amp;y=2"', transformed)
        self.assertEqual(targets, {self.duration.source})

    def test_section_fragments_become_html_links_and_mdoc_references(self):
        """Preserve and validate HTML fragments; mdoc references the full page."""
        href = "../concepts/storage-duration.md#static-storage-duration"
        html, _ = rewrite_html(f'<body><a href="{href}">section</a></body>',
                               self.resolver, self.static)
        mdoc, _ = rewrite_mdoc(f'.Lk {href} section\n',
                               self.resolver, self.static)
        self.assertIn('href="c-storage-duration.7.html#static-storage-duration"', html)
        self.assertEqual(mdoc, '.Xr c-storage-duration 7\n')

    def test_rejects_unwritten_pages(self):
        """Never silently emit a broken link to a planned but absent manual."""
        with self.assertRaisesRegex(ReferenceError, "missing manual link"):
            self.resolver.resolve("../concepts/linkage.md", self.static)

    def test_rejects_missing_target_fragment(self):
        """Lowdown's actual IDs determine what fragment names are allowed."""
        with self.assertRaisesRegex(ReferenceError, "unknown section"):
            self.resolver.resolve("../concepts/storage-duration.md#bad-heading", self.static)

    def test_validates_same_page_fragment(self):
        """Fragment-only links keep their href but must point to real IDs."""
        self.assertIsNone(self.resolver.resolve("#name", self.static))
        with self.assertRaisesRegex(ReferenceError, "unknown section"):
            self.resolver.resolve("#missing", self.static)

    def test_rejects_queries_and_absolute_paths_for_manuals(self):
        """Manual references must remain predictable relative source links."""
        for href in ("../concepts/storage-duration.md?raw=1", "/pages/concepts/static.md"):
            with self.subTest(href=href), self.assertRaises(ReferenceError):
                self.resolver.resolve(href, self.static)

    def test_unrelated_links_remain_unmodified(self):
        """External sites and PDF attachments are not documentation pages."""
        original = '.Lk https://example.org reference\n.Lk docs.pdf bibliography\n'
        transformed, targets = rewrite_mdoc(original, self.resolver, self.static)
        self.assertEqual(original, transformed)
        self.assertEqual(targets, set())


if __name__ == "__main__":
    unittest.main()

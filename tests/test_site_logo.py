"""Check the shared, offline SVG logo used by all generated HTML pages."""
from pathlib import Path
from tempfile import TemporaryDirectory
import sys
import unittest
from xml.etree import ElementTree

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from build_pages import copy_atomic, needs_build  # noqa: E402


class SiteLogoTests(unittest.TestCase):
    def test_svg_uses_outlined_path(self):
        logo = ROOT / "assets" / "c-logo.svg"
        root = ElementTree.parse(logo).getroot()
        namespace = "{http://www.w3.org/2000/svg}"
        self.assertEqual(root.tag, namespace + "svg")
        self.assertTrue(root.findall(".//" + namespace + "path"))
        self.assertFalse(root.findall(".//" + namespace + "text"))

    def test_templates_use_local_asset(self):
        for filename in ("index.html", "page.html"):
            with self.subTest(template=filename):
                content = (ROOT / "templates" / filename).read_text(encoding="utf-8")
                self.assertIn('<link rel="icon" type="image/svg+xml" href="c-logo.svg">', content)
        home = (ROOT / "templates" / "index.html").read_text(encoding="utf-8")
        self.assertIn('class="home-mark-logo" src="c-logo.svg"', home)

    def test_asset_copy_is_incremental(self):
        source = ROOT / "assets" / "c-logo.svg"
        with TemporaryDirectory() as directory:
            output = Path(directory) / "c-logo.svg"
            self.assertTrue(needs_build(output, (source,)))
            copy_atomic(source, output)
            self.assertEqual(output.read_bytes(), source.read_bytes())
            self.assertFalse(needs_build(output, (source,)))


if __name__ == "__main__":
    unittest.main()

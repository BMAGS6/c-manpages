"""Verify linked Markdown tables survive both production converters.

This integration check is optional on hosts without lowdown or mandoc. The
unit tests in test_xrefs.py cover the transformation independently.
"""

from pathlib import Path
import shutil
import subprocess
import sys
from tempfile import TemporaryDirectory
from types import SimpleNamespace
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from xrefs import ReferenceResolver, rewrite_html, rewrite_mdoc  # noqa: E402


@unittest.skipUnless(shutil.which("lowdown") and shutil.which("mandoc"),
                     "requires lowdown and mandoc")
class TableLinkPipelineTests(unittest.TestCase):
    def test_real_converters_keep_html_links_and_plain_mdoc_table_cells(self):
        """An HTML table link remains clickable; its mdoc counterpart validates."""
        with TemporaryDirectory() as directory:
            root = Path(directory)
            keyword_dir = root / "pages" / "keywords"
            keyword_dir.mkdir(parents=True)
            index_source = keyword_dir / "keywords.md"
            target_source = keyword_dir / "static.md"
            target_source.write_text(
                "title: c-static\nsection: 7\nsource: C Manual Pages\n\n"
                "# NAME\n\nc-static -- a test manual\n",
                encoding="utf-8",
            )
            index_source.write_text(
                "title: c-keywords\nsection: 7\nsource: C Manual Pages\n\n"
                "# NAME\n\nc-keywords -- test keyword index\n\n"
                "# KEYWORDS AT A GLANCE\n\n"
                "| Keyword | External |\n"
                "| :------ | :------- |\n"
                "| [`static`](static.md) | [Reference](https://example.org/) |\n\n"
                "See [`static`](static.md) for details.\n",
                encoding="utf-8",
            )
            index = SimpleNamespace(
                source=index_source, name="c-keywords", section="7",
                html_path=root / "build" / "html" / "c-keywords.7.html",
            )
            target = SimpleNamespace(
                source=target_source, name="c-static", section="7",
                html_path=root / "build" / "html" / "c-static.7.html",
            )

            def lowdown(fmt, source):
                args = ["lowdown", "-s", "-t", fmt]
                if fmt == "mdoc":
                    args.append("--roff-manpage")
                return subprocess.run(
                    [*args, str(source)], capture_output=True, text=True,
                    check=True,
                ).stdout

            resolver = ReferenceResolver([index, target],
                                         lambda page: lowdown("html", page.source))
            transformed, targets = rewrite_mdoc(lowdown("mdoc", index_source),
                                                resolver, index)
            self.assertEqual(targets, {target_source})
            self.assertIn(".TS", transformed)
            self.assertIn(".TE", transformed)
            table = transformed.split(".TS", 1)[1].split(".TE", 1)[0]
            self.assertIn("static", table)
            self.assertNotIn(".Lk ", table)
            self.assertNotIn(".Mt ", table)
            self.assertNotIn(".Xr ", table)
            self.assertIn(".Xr c-static 7", transformed.split(".TE", 1)[1])

            html, html_targets = rewrite_html(lowdown("html", index_source),
                                              resolver, index)
            self.assertIn('href="c-static.7.html"', html)
            self.assertEqual(html_targets, {target_source})

            output = root / "c-keywords.7"
            output.write_text(transformed, encoding="utf-8")
            checked = subprocess.run(
                ["mandoc", "-W", "error", "-T", "utf8", str(output)],
                capture_output=True, text=True, check=False,
            )
            self.assertEqual(checked.returncode, 0, checked.stderr)


if __name__ == "__main__":
    unittest.main()

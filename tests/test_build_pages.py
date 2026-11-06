"""Tests for automatic page discovery and the multi-page build coordinator.

The tests create a disposable project tree and fake lowdown/mandoc commands.
This exercises command construction, output names, and incremental behavior
without depending on those Unix tools being installed on the test machine.
Run the real tools on the development computer with ``make check html``.
"""

from contextlib import redirect_stderr, redirect_stdout
import io
import os
from pathlib import Path
import shutil
import sys
import tempfile
import unittest


PROJECT_ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(PROJECT_ROOT / "tools"))
import build_pages  # noqa: E402  # Imported from the project, not a package.


class BuildTests(unittest.TestCase):
    """Exercise a fresh disposable documentation tree for every test."""

    def setUp(self):
        """Install temporary inputs and redirect build paths to their tree."""
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.root = Path(temp.name)
        self.pages_dir = self.root / "pages"
        self.pages_dir.mkdir()

        # Patch module path constants so integration tests never touch the
        # developer's real build directory or source documents.
        replacements = {
            "ROOT": self.root,
            "PAGE_DIR": self.pages_dir,
            "BUILD_DIR": self.root / "build",
            "THIS_FILE": self.root / "tools" / "build_pages.py",
            "HTML_BUILDER": self.root / "tools" / "build_html.py",
            "HIGHLIGHTER": self.root / "tools" / "highlight_html.py",
            "HTML_TEMPLATE": self.root / "templates" / "page.html",
            "SITE_CSS": self.root / "assets" / "site.css",
        }
        previous = {key: getattr(build_pages, key) for key in replacements}
        for key, value in replacements.items():
            setattr(build_pages, key, value)
        self.addCleanup(
            lambda: [setattr(build_pages, key, value) for key, value in previous.items()]
        )

        # Reuse the actual production HTML builder and template. The mocked
        # lowdown command produces a minimal standalone HTML fixture for it.
        for relative in (
            "tools/build_pages.py", "tools/build_html.py", "tools/highlight_html.py",
            "templates/page.html", "assets/site.css",
        ):
            destination = self.root / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(PROJECT_ROOT / relative, destination)

        self.fake_lowdown = self.root / "fake-lowdown"
        self.fake_lowdown.write_text(
            "#!/usr/bin/env python3\n"
            "import pathlib, sys\n"
            "source = pathlib.Path(sys.argv[-1]).read_text()\n"
            "if 'FAIL_CONVERSION' in source:\n"
            "    print('simulated converter failure', file=sys.stderr)\n"
            "    sys.exit(2)\n"
            "name = source.splitlines()[0].split(': ', 1)[1]\n"
            "if 'mdoc' in sys.argv:\n"
            "    print('.Dd November 6, 2026\\n.Dt ' + name + ' 7\\n.Sh NAME')\n"
            "else:\n"
            "    print('<html><head><title>' + name + '</title></head>'\n"
            "          '<body><h1 id=\"name\">NAME</h1><p>Test</p></body></html>')\n",
            encoding="utf-8",
        )
        self.fake_lowdown.chmod(0o755)
        self.fake_mandoc = self.root / "fake-mandoc"
        self.fake_mandoc.write_text(
            "#!/usr/bin/env python3\n"
            "import sys\n"
            "if 'lint' in sys.argv:\n"
            "    print('simulated stylistic warning', file=sys.stderr)\n"
            "if 'html' in sys.argv:\n"
            "    print('<html><body>plain mandoc</body></html>')\n"
            "else:\n"
            "    print('rendered manual')\n",
            encoding="utf-8",
        )
        self.fake_mandoc.chmod(0o755)

    def make_source(self, category, stem, *, title=None):
        """Create a minimal, metadata-bearing canonical Markdown document."""
        destination = self.pages_dir / category / f"{stem}.md"
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_text(
            f"title: {title or f'c-{stem}'}\n"
            "section: 7\n"
            "source: C Manual Pages\n\n"
            "# NAME\n\n"
            f"c-{stem} -- a test page\n",
            encoding="utf-8",
        )
        return destination

    def builder(self):
        """Construct a builder that runs disposable mock executables."""
        return build_pages.Builder(
            build_pages.discover_pages(),
            lowdown=str(self.fake_lowdown),
            mandoc=str(self.fake_mandoc),
        )

    def test_discovers_multiple_pages_without_registration(self):
        """Different directories share one automatic Unix man namespace."""
        self.make_source("keywords", "static")
        self.make_source("concepts", "storage-duration")
        found = build_pages.discover_pages()
        self.assertEqual({page.name for page in found}, {"c-static", "c-storage-duration"})
        self.assertEqual(
            {page.man_path.name for page in found},
            {"c-static.7", "c-storage-duration.7"},
        )

    def test_rejects_duplicate_page_names(self):
        """Identically named sources must not overwrite each other's output."""
        self.make_source("concepts", "static")
        self.make_source("keywords", "static")
        with self.assertRaisesRegex(build_pages.BuildError, "duplicate page name"):
            build_pages.discover_pages()

    def test_rejects_incorrect_title(self):
        """Explicit lowdown title metadata must match the generated path."""
        self.make_source("keywords", "static", title="c-something-else")
        with self.assertRaisesRegex(build_pages.BuildError, "metadata title must"):
            build_pages.discover_pages()

    def test_rejects_invalid_filename(self):
        """Avoid spaces/underscores that lead to ambiguous man identifiers."""
        self.make_source("concepts", "Storage_Duration")
        with self.assertRaisesRegex(build_pages.BuildError, "filename must"):
            build_pages.discover_pages()

    def test_rejects_missing_metadata(self):
        """Missing lowdown section metadata must stop the build clearly."""
        source = self.make_source("keywords", "static")
        source.write_text("title: c-static\nsource: C Manual Pages\n\n# NAME\n")
        with self.assertRaisesRegex(build_pages.BuildError, "missing required metadata"):
            build_pages.discover_pages()

    def test_builds_all_formats_and_skips_unchanged_files(self):
        """Run the real coordinator with two sources and mocked converters."""
        self.make_source("keywords", "static")
        self.make_source("concepts", "storage-duration")
        builder = self.builder()

        with redirect_stdout(io.StringIO()):
            builder.build_man()
            builder.build_html()
            builder.lint()
            builder.build_highlighted()
            builder.build_mandoc_html()

        expected = [
            self.root / "build/man7/c-static.7",
            self.root / "build/man7/c-storage-duration.7",
            self.root / "build/html/c-static.7.html",
            self.root / "build/html/c-storage-duration.7.html",
            self.root / "build/html/c-static-highlighted.html",
            self.root / "build/html/c-storage-duration-highlighted.html",
            self.root / "build/html/c-static-mandoc.html",
            self.root / "build/html/c-storage-duration-mandoc.html",
        ]
        for output in expected:
            self.assertTrue(output.is_file(), str(output))

        html = (self.root / "build/html/c-static.7.html").read_text()
        self.assertIn("C Manual Pages", html)
        self.assertIn("Skip to content", html)

        timestamps = {path: path.stat().st_mtime_ns for path in expected}
        with redirect_stdout(io.StringIO()):
            builder.build_man()
            builder.build_html()
            builder.build_highlighted()
            builder.build_mandoc_html()
        self.assertEqual(
            timestamps, {path: path.stat().st_mtime_ns for path in expected}
        )

    def test_strict_lint_shows_warnings(self):
        """Preserve visible diagnostics from mandoc even on zero exit status."""
        self.make_source("keywords", "static")
        builder = self.builder()
        diagnostics = io.StringIO()
        with redirect_stdout(io.StringIO()), redirect_stderr(diagnostics):
            builder.lint(strict=True)
        self.assertIn("simulated stylistic warning", diagnostics.getvalue())

    def test_css_rebuilds_html_but_not_man(self):
        """A presentational edit must not needlessly reconvert mdoc output."""
        self.make_source("keywords", "static")
        builder = self.builder()
        with redirect_stdout(io.StringIO()):
            builder.build_man()
            builder.build_html()
        man_page = builder.pages[0].man_path
        html_page = builder.pages[0].html_path
        before_man = man_page.stat().st_mtime_ns
        before_html = html_page.stat().st_mtime_ns

        css = build_pages.SITE_CSS
        css.write_text(css.read_text() + "\n/* Test change. */\n")
        # Explicitly advance the dependency timestamp to avoid coarse-mtime
        # filesystems making two immediate writes appear simultaneous.
        later = max(css.stat().st_mtime_ns, before_html) + 2_000_000_000
        os.utime(css, ns=(later, later))
        with redirect_stdout(io.StringIO()):
            builder.build_man()
            builder.build_html()
        self.assertEqual(before_man, man_page.stat().st_mtime_ns)
        self.assertNotEqual(before_html, html_page.stat().st_mtime_ns)

    def test_failed_conversion_preserves_last_good_output(self):
        """Converter failures must not replace successful previous output."""
        source = self.make_source("keywords", "static")
        builder = self.builder()
        with redirect_stdout(io.StringIO()):
            builder.build_man()
        output = builder.pages[0].man_path
        previous_bytes = output.read_bytes()

        source.write_text(source.read_text() + "\nFAIL_CONVERSION\n")
        later = max(source.stat().st_mtime_ns, output.stat().st_mtime_ns) + 2_000_000_000
        os.utime(source, ns=(later, later))
        with self.assertRaisesRegex(build_pages.BuildError, "simulated converter failure"):
            builder.build_man()
        self.assertEqual(previous_bytes, output.read_bytes())


if __name__ == "__main__":
    unittest.main()

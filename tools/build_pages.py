#!/usr/bin/env python3
"""Build every C Manual Pages document without GNU Make extensions.

This module performs five deliberately narrow jobs:

1. Discover Markdown files beneath ``pages/``.
2. Validate the existing lowdown metadata and unique Unix manual identities.
3. Invoke the project's existing lowdown/HTML/mandoc conversion pipeline.
4. Rebuild only outputs whose input files have changed.
5. Report errors without overwriting a previously successful build artifact.

Cross-reference resolution is delegated to ``tools/xrefs.py``. The POSIX
Makefile remains the public interface; this coordinator supplies automatic
discovery and dependency tracking without GNU Make extensions.
"""

import argparse
from dataclasses import dataclass
from string import Template
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

from xrefs import ReferenceError, ReferenceResolver, rewrite_html, rewrite_mdoc
from site_navigation import make_catalog, render_navigation, render_index_categories


# Resolve from this file rather than the current working directory. This also
# supports calls like ``make -C /some/path/to/c-manpages``.
ROOT = Path(__file__).resolve().parent.parent
PAGE_DIR = ROOT / "pages"
BUILD_DIR = ROOT / "build"
THIS_FILE = Path(__file__).resolve()
HTML_BUILDER = ROOT / "tools" / "build_html.py"
HIGHLIGHTER = ROOT / "tools" / "highlight_html.py"
XREFS = ROOT / "tools" / "xrefs.py"
HTML_TEMPLATE = ROOT / "templates" / "page.html"
SITE_CSS = ROOT / "assets" / "site.css"
SITE_NAVIGATION = ROOT / "tools" / "site_navigation.py"
INDEX_TEMPLATE = ROOT / "templates" / "index.html"
SITE_CATALOG = BUILD_DIR / "site-catalog.json"
SITE_INDEX = BUILD_DIR / "html" / "index.html"

# Restrict page filenames to predictable, portable Unix-man identifiers.
STEM_PATTERN = re.compile(r"[a-z][a-z0-9]*(?:-[a-z0-9]+)*\Z")
HEADER_PATTERN = re.compile(r"([a-z][a-z0-9_-]*):[ \t]*(\S.*?)\s*\Z")


class BuildError(Exception):
    """Indicate an invalid source document or a failed conversion command."""


@dataclass(frozen=True)
class Page:
    """Describe one page's source and unique installed-manual identity."""

    source: Path
    name: str
    section: str

    @property
    def man_path(self):
        """Return ``build/man7/c-static.7``, for example."""
        return BUILD_DIR / f"man{self.section}" / f"{self.name}.{self.section}"

    @property
    def html_path(self):
        """Return the enhanced, standalone HTML output path."""
        return BUILD_DIR / "html" / f"{self.name}.{self.section}.html"

    @property
    def highlighted_path(self):
        """Return the previous experimental highlighted-HTML filename."""
        return BUILD_DIR / "html" / f"{self.name}-highlighted.html"

    @property
    def mandoc_html_path(self):
        """Return the unstyled mandoc-generated HTML comparison path."""
        return BUILD_DIR / "html" / f"{self.name}-mandoc.html"


def read_metadata(source):
    """Read lowdown's blank-line-terminated key/value metadata preamble.

    This is deliberately *not* YAML front matter: the current project already
    uses simple ``title:``, ``section:``, and ``source:`` lines. Requiring them
    avoids producing a man page with the wrong title or manual section.
    """
    values = {}
    lines = source.read_text(encoding="utf-8").splitlines()

    for number, line in enumerate(lines, start=1):
        if not line.strip():
            break

        match = HEADER_PATTERN.fullmatch(line)
        if match is None:
            raise BuildError(
                f"{source}:{number}: expected 'key: value' metadata "
                "followed by a blank line"
            )
        key, value = match.groups()
        if key in values:
            raise BuildError(f"{source}:{number}: repeated metadata field '{key}'")
        values[key] = value
    else:
        raise BuildError(f"{source}: metadata must end with a blank line")

    for key in ("title", "section", "source"):
        if key not in values:
            raise BuildError(f"{source}: missing required metadata field '{key}'")
    return values


def discover_pages(page_dir=None):
    """Find sources in a stable order and reject conflicting man-page names.

    Two categories cannot each contain ``static.md``: both would map to the
    same ``c-static(7)`` Unix manual and overwrite one another. Metadata is
    checked even when all generated outputs are already up to date.
    """
    if page_dir is None:
        page_dir = PAGE_DIR
    if not page_dir.is_dir():
        raise BuildError(f"missing source directory: {page_dir}")

    pages = []
    seen = {}
    for source in sorted(page_dir.rglob("*.md")):
        if not STEM_PATTERN.fullmatch(source.stem):
            raise BuildError(
                f"{source}: filename must be lowercase and hyphen-separated"
            )
        name = f"c-{source.stem}"
        if name in seen:
            raise BuildError(f"duplicate page name {name}: {seen[name]} and {source}")

        metadata = read_metadata(source)
        if metadata["title"] != name:
            raise BuildError(
                f"{source}: metadata title must be '{name}', "
                f"not '{metadata['title']}'"
            )
        section = metadata["section"]
        if re.fullmatch(r"[1-9]", section) is None:
            raise BuildError(f"{source}: section must be a single digit from 1 to 9")

        seen[name] = source
        pages.append(Page(source, name, section))

    if not pages:
        raise BuildError(f"no Markdown documents found in {page_dir}")
    return pages


def needs_build(output, dependencies):
    """Detect an absent output or a newer format-specific dependency.

    mtime comparisons avoid recomputing unaffected pages. For example, a CSS
    change rebuilds HTML but cannot change a Unix terminal manual. Nanosecond
    timestamps are used wherever the filesystem supports them.
    """
    if not output.is_file():
        return True
    last_build = output.stat().st_mtime_ns
    for dependency in dependencies:
        if not dependency.is_file():
            raise BuildError(f"missing build input: {dependency}")
        if dependency.stat().st_mtime_ns > last_build:
            return True
    return False


def capture(arguments, input_text=None, echo_stderr=False):
    """Run a converter without a shell, retaining stderr on failure.

    Explicit argv lists avoid escaping problems. Capturing the entire output
    before writing a file ensures that failed conversions do not replace a
    previously valid page with truncated data. With echo_stderr=True,
    successful commands also show their nonfatal diagnostics (strict lint).
    """
    try:
        result = subprocess.run(
            arguments,
            input=input_text,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            encoding="utf-8",
            check=False,
        )
    except OSError as error:
        raise BuildError(f"could not execute {arguments[0]}: {error}") from error

    if result.returncode:
        details = result.stderr.strip() or result.stdout.strip() or "no diagnostic output"
        raise BuildError(
            f"command failed (exit {result.returncode}): {' '.join(arguments)}\n{details}"
        )
    if echo_stderr and result.stderr:
        sys.stderr.write(result.stderr)
    return result.stdout


def write_atomic(path, contents):
    """Publish completed text without exposing a partly written output.

    The temporary file shares the destination directory, making os.replace()
    atomic on the same filesystem. Always delete temporary files after errors.
    """
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(f".{path.name}.tmp")
    try:
        temporary.write_text(contents, encoding="utf-8")
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)


def write_if_changed(path, contents):
    """Update generated metadata only when its *contents* actually change.

    Every call to ``make html`` regenerates the catalog from discovered pages.
    Rewriting it unconditionally would make all pages appear stale on every
    build and defeat incremental generation. We therefore compare content
    before publishing an atomic replacement. Deleting or adding a page
    changes the catalog and triggers exactly the necessary HTML rebuilds.
    """
    if path.is_file() and path.read_text(encoding="utf-8") == contents:
        return False
    write_atomic(path, contents)
    return True


def copy_atomic(source, destination):
    """Publish a compatibility copy with the same atomic replacement rule."""
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = destination.with_name(f".{destination.name}.tmp")
    try:
        shutil.copyfile(source, temporary)
        os.replace(temporary, destination)
    finally:
        temporary.unlink(missing_ok=True)


class Builder:
    """Apply one requested output format to all registered manual pages.

    Phase B records direct manual dependencies in small generated JSON files.
    Each format maintains its own dependency list because an HTML rebuild and
    a roff rebuild can happen independently. Sidecars are saved in ``build/``
    (never the canonical Markdown directory) and removed by ``make clean``.

    If a linked page changes, referencing documents are rebuilt to revalidate
    fragment anchors. A deleted linked page is reported even if the referring
    document itself has not changed. This avoids quietly serving broken links
    from stale incremental output.
    """

    def __init__(self, pages, lowdown="lowdown", mandoc="mandoc", man="man"):
        self.pages = pages
        self.lowdown = lowdown
        self.mandoc = mandoc
        self.man = man
        self.references = ReferenceResolver(pages, self.raw_html)

    def raw_html(self, page):
        """Ask lowdown for the exact HTML used to validate fragment IDs."""
        return capture([self.lowdown, "-s", "-t", "html", str(page.source)])

    def dependency_path(self, page, format_name):
        """Locate the generated, per-format manifest for a manual page."""
        return BUILD_DIR / "references" / format_name / f"{page.name}.{page.section}.json"

    def saved_references(self, page, format_name):
        """Read previously resolved target paths; return None if unavailable.

        This sidecar is a cache, not an authoritative document. Missing or
        invalid files are regenerated during the next successful conversion.
        Relative paths keep generated metadata independent of the checkout's
        absolute location.
        """
        sidecar = self.dependency_path(page, format_name)
        if not sidecar.is_file():
            return None
        try:
            values = json.loads(sidecar.read_text(encoding="utf-8"))
            if not isinstance(values, dict) or not isinstance(values.get("targets"), list):
                return None
            paths = values["targets"]
            if not all(isinstance(path, str) and path.startswith("pages/") for path in paths):
                return None
            return [(ROOT / path).resolve() for path in paths]
        except (ValueError, OSError, UnicodeError):
            return None

    def should_build(self, page, format_name, output, dependencies):
        """Check direct inputs first, then transitive manual-page inputs.

        Checking the author's source before saved dependencies matters: when a
        link has been removed from Markdown, the old linked file may also have
        been deleted. We must rebuild the changed authoring source rather than
        incorrectly fail using an obsolete sidecar.
        """
        sidecar = self.dependency_path(page, format_name)
        direct = (page.source, THIS_FILE, XREFS, sidecar, *dependencies)
        stored = self.saved_references(page, format_name)
        if stored is None or needs_build(output, direct):
            return True

        registered = self.references.pages_by_path
        for target in stored:
            if target not in registered or not target.is_file():
                raise BuildError(
                    f"{page.source}: previously linked manual page is missing: {target}"
                )
        return needs_build(output, stored)

    def publish(self, page, format_name, output, contents, targets):
        """Publish a converted page and the links that affect its freshness.

        Write the manifest first, then the finished page. If the final write
        fails, the manifest is newer than the last good output, forcing a
        retry. Conversion errors occur before either file is touched.
        """
        sidecar = self.dependency_path(page, format_name)
        paths = sorted(str(target.relative_to(ROOT)) for target in targets)
        manifest = json.dumps({"targets": paths}, indent=2) + "\n"
        write_atomic(sidecar, manifest)
        write_atomic(output, contents)

    def build_man(self):
        """Generate mdoc and replace only registered internal .Lk references."""
        for page in self.pages:
            if not self.should_build(page, "man", page.man_path, ()):
                continue
            result = capture([
                self.lowdown, "-s", "-t", "mdoc", "--roff-manpage", str(page.source)
            ])
            if not result.strip():
                raise BuildError(f"empty mdoc output from {page.source}")
            try:
                transformed, targets = rewrite_mdoc(result, self.references, page)
            except ReferenceError as error:
                raise BuildError(str(error)) from error
            self.publish(page, "man", page.man_path, transformed, targets)
            print(f"MAN   {page.man_path.relative_to(ROOT)}")

    def build_catalog(self):
        """Publish a stable navigation registry derived from all source pages.

        This is a build artifact (not a second source of truth). Its timestamp
        changes only when pages are added, removed, renamed, or recategorized.
        Every generated page embeds navigation drawn from this same registry.
        """
        catalog = make_catalog(self.pages, PAGE_DIR)
        content = json.dumps(catalog, indent=2, ensure_ascii=False) + "\n"
        write_if_changed(SITE_CATALOG, content)
        return catalog

    def build_site_index(self, catalog):
        """Generate the offline homepage and category index incrementally.

        The homepage uses the exact same navigation as documentation pages,
        and its index links are computed from the registered page identities.
        Existing valid output is not overwritten if rendering fails.
        """
        dependencies = (SITE_CATALOG, INDEX_TEMPLATE, SITE_CSS, SITE_NAVIGATION, THIS_FILE)
        if not needs_build(SITE_INDEX, dependencies):
            return
        template = Template(INDEX_TEMPLATE.read_text(encoding="utf-8"))
        rendered = template.substitute(
            site_css=SITE_CSS.read_text(encoding="utf-8"),
            navigation=render_navigation(catalog),
            categories=render_index_categories(catalog),
            page_count=str(len(catalog)),
        )
        write_atomic(SITE_INDEX, rendered)
        print(f"HTML  {SITE_INDEX.relative_to(ROOT)}")

    def build_html(self):
        """Build all enhanced HTML pages with consistent, fresh site navigation.

        Building the catalog first means an added/removed page updates both
        the homepage and all existing sidebars. Page-specific references are
        still validated by the Phase B resolver before anything is published.
        """
        catalog = self.build_catalog()
        shared_inputs = (
            HTML_BUILDER, HIGHLIGHTER, HTML_TEMPLATE, SITE_CSS,
            SITE_CATALOG, SITE_NAVIGATION,
        )
        for page in self.pages:
            if not self.should_build(page, "html", page.html_path, shared_inputs):
                continue
            raw = self.raw_html(page)
            if not raw.strip():
                raise BuildError(f"empty lowdown HTML output from {page.source}")
            try:
                transformed, targets = rewrite_html(raw, self.references, page)
            except ReferenceError as error:
                raise BuildError(str(error)) from error
            final = capture([
                sys.executable, str(HTML_BUILDER),
                "--template", str(HTML_TEMPLATE),
                "--css", str(SITE_CSS),
                "--catalog", str(SITE_CATALOG),
                "--section", page.section,
                "--page-name", page.name,
            ], input_text=transformed)
            if not final.strip():
                raise BuildError(f"empty enhanced HTML output from {page.source}")
            self.publish(page, "html", page.html_path, final, targets)
            print(f"HTML  {page.html_path.relative_to(ROOT)}")
        self.build_site_index(catalog)

    def lint(self, strict=False):
        """Check every manual, retaining the previous strict/non-strict modes.

        Normal mode renders using ``mandoc -W error -T utf8`` to identify real
        errors without failing on converter-generated stylistic warnings.
        Strict mode exposes every ``mandoc -T lint`` diagnostic.
        """
        self.build_man()
        for page in self.pages:
            if strict:
                args = [self.mandoc, "-T", "lint", str(page.man_path)]
            else:
                args = [self.mandoc, "-W", "error", "-T", "utf8", str(page.man_path)]
            output = capture(args, echo_stderr=strict)
            if strict and output:
                print(output, end="" if output.endswith("\n") else "\n")
            print(f"CHECK {page.name}({page.section})")

    def build_highlighted(self):
        """Maintain the old *-highlighted.html comparison target per page."""
        self.build_html()
        for page in self.pages:
            if needs_build(page.highlighted_path, (page.html_path,)):
                copy_atomic(page.html_path, page.highlighted_path)
                print(f"COPY  {page.highlighted_path.relative_to(ROOT)}")

    def build_mandoc_html(self):
        """Generate optional plain mandoc HTML as before, for every page."""
        self.build_man()
        for page in self.pages:
            if not needs_build(page.mandoc_html_path, (page.man_path, THIS_FILE)):
                continue
            content = capture([
                self.mandoc, "-T", "html", "-O", "man=%N.%S.html", str(page.man_path)
            ])
            write_atomic(page.mandoc_html_path, content)
            print(f"HTML  {page.mandoc_html_path.relative_to(ROOT)}")

    def view(self, page_name):
        """Display the selected manual using ``man -l`` and the user's pager."""
        self.build_man()
        matched = [page for page in self.pages if page.name == page_name]
        if not matched:
            raise BuildError(f"unknown manual '{page_name}'; run 'make list-pages'")
        try:
            result = subprocess.run(
                [self.man, "-l", str(matched[0].man_path)], check=False
            )
        except OSError as error:
            raise BuildError(f"could not execute {self.man}: {error}") from error
        if result.returncode:
            raise BuildError(f"man exited with status {result.returncode}")


def main():
    """Expose stable actions for the Makefile; return nonzero on any error."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--lowdown", default="lowdown", help="lowdown executable")
    parser.add_argument("--mandoc", default="mandoc", help="mandoc executable")
    parser.add_argument("--man", default="man", help="man executable")
    parser.add_argument("--page", default="c-static", help="page opened by view")
    parser.add_argument(
        "action",
        choices=("man", "html", "lint", "lint-strict", "html-highlighted",
                 "html-mandoc", "view", "list"),
    )
    args = parser.parse_args()
    try:
        pages = discover_pages()
        builder = Builder(pages, args.lowdown, args.mandoc, args.man)
        if args.action == "man":
            builder.build_man()
        elif args.action == "html":
            builder.build_html()
        elif args.action == "lint":
            builder.lint()
        elif args.action == "lint-strict":
            builder.lint(strict=True)
        elif args.action == "html-highlighted":
            builder.build_highlighted()
        elif args.action == "html-mandoc":
            builder.build_mandoc_html()
        elif args.action == "view":
            builder.view(args.page)
        else:
            for page in pages:
                print(f"{page.name}({page.section})  {page.source.relative_to(ROOT)}")
    except (BuildError, UnicodeError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env python3
"""Build every C Manual Pages document without GNU Make extensions.

This module performs five deliberately narrow jobs:

1. Discover Markdown files beneath ``pages/``.
2. Validate the existing lowdown metadata and unique Unix manual identities.
3. Invoke the project's existing lowdown/HTML/mandoc conversion pipeline.
4. Rebuild only outputs whose input files have changed.
5. Report errors without overwriting a previously successful build artifact.

It does *not* parse arbitrary Markdown or implement cross-references. Those
features belong to later milestones. The POSIX Makefile is the public interface;
this small coordinator supplies dynamic discovery, which POSIX make lacks.
"""

import argparse
from dataclasses import dataclass
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys


# Resolve from this file rather than the current working directory. This also
# supports calls like ``make -C /some/path/to/c-manpages``.
ROOT = Path(__file__).resolve().parent.parent
PAGE_DIR = ROOT / "pages"
BUILD_DIR = ROOT / "build"
THIS_FILE = Path(__file__).resolve()
HTML_BUILDER = ROOT / "tools" / "build_html.py"
HIGHLIGHTER = ROOT / "tools" / "highlight_html.py"
HTML_TEMPLATE = ROOT / "templates" / "page.html"
SITE_CSS = ROOT / "assets" / "site.css"

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
    """Apply one requested format/action to the shared discovered page list."""

    def __init__(self, pages, lowdown="lowdown", mandoc="mandoc", man="man"):
        self.pages = pages
        self.lowdown = lowdown
        self.mandoc = mandoc
        self.man = man

    def build_man(self):
        """Generate the same lowdown mdoc output as the original Makefile."""
        for page in self.pages:
            if not needs_build(page.man_path, (page.source, THIS_FILE)):
                continue
            result = capture([
                self.lowdown, "-s", "-t", "mdoc", "--roff-manpage", str(page.source)
            ])
            if not result.strip():
                raise BuildError(f"empty mdoc output from {page.source}")
            write_atomic(page.man_path, result)
            print(f"MAN   {page.man_path.relative_to(ROOT)}")

    def build_html(self):
        """Run the existing lowdown and Pygments/template HTML pipeline."""
        shared_inputs = (THIS_FILE, HTML_BUILDER, HIGHLIGHTER, HTML_TEMPLATE, SITE_CSS)
        for page in self.pages:
            if not needs_build(page.html_path, (page.source, *shared_inputs)):
                continue
            raw = capture([self.lowdown, "-s", "-t", "html", str(page.source)])
            if not raw.strip():
                raise BuildError(f"empty lowdown HTML output from {page.source}")
            final = capture([
                sys.executable, str(HTML_BUILDER),
                "--template", str(HTML_TEMPLATE),
                "--css", str(SITE_CSS), "--section", page.section,
            ], input_text=raw)
            if not final.strip():
                raise BuildError(f"empty enhanced HTML output from {page.source}")
            write_atomic(page.html_path, final)
            print(f"HTML  {page.html_path.relative_to(ROOT)}")

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

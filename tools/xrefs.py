#!/usr/bin/env python3
"""Resolve canonical Markdown links into C manual cross-references.

Why this module exists
----------------------
Lowdown intentionally treats ``[text](../concepts/example.md)`` as an ordinary
hyperlink. Its HTML renderer emits ``<a href=...>``; its mdoc renderer emits a
``.Lk`` macro. Neither knows that the destination is another C manual page.

We reuse lowdown's *parsed* links instead of guessing what Markdown contains:

* A small HTMLParser rewrites only actual ``href`` attributes on anchors.
* The mdoc transformer reads complete ``.Lk`` macro lines and changes *only*
  links pointing to registered Markdown documentation into ``.Xr`` macros.
* Both transformations share one path resolver, so they cannot accidentally
  disagree about an internal page's identifier or manual section.

External hyperlinks, non-Markdown files, and normal Markdown source files are
left alone. Missing internal .md destinations are errors, never silent links.
The original Markdown is neither altered nor given proprietary directives.

A cross-page section fragment survives in HTML (``file.7.html#section``). The
mdoc ``.Xr`` reference denotes the whole manual page, because ``.Xr`` has no
argument for an HTML-style section fragment. The fragment is still validated.
"""

from html import escape
from html.parser import HTMLParser
from pathlib import Path
from urllib.parse import unquote, urlsplit


class ReferenceError(Exception):
    """A Markdown page links to an invalid or unavailable documentation page."""


class AnchorCollector(HTMLParser):
    """Collect HTML ``id`` attributes directly from lowdown's rendered output.

    Reusing generated IDs avoids maintaining our own approximate implementation
    of lowdown's heading-to-anchor algorithm. This also recognizes explicit IDs
    on other HTML elements, not just headings.
    """

    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.anchors = set()

    def handle_starttag(self, tag, attrs):
        identifier = dict(attrs).get("id")
        if identifier is not None:
            self.anchors.add(identifier)

    def handle_startendtag(self, tag, attrs):
        self.handle_starttag(tag, attrs)


def extract_anchors(document):
    """Return anchor IDs emitted by lowdown for one complete HTML document."""
    parser = AnchorCollector()
    parser.feed(document)
    parser.close()
    return parser.anchors


class ReferenceResolver:
    """Map a relative Markdown link to a known Page and optional fragment.

    ``pages`` is the same validated page registry built by ``discover_pages``.
    ``load_html(page)`` produces lowdown's raw standalone HTML for a page. It is
    called only when a fragment needs validation, and results are cached by
    source modification time so repeated links need not rerun the converter.

    The resolver doesn't depend on the Page class itself. A page simply needs
    ``source``, ``name``, ``section``, and ``html_path`` attributes.
    """

    def __init__(self, pages, load_html):
        self.pages_by_path = {page.source.resolve(): page for page in pages}
        self.load_html = load_html
        self.anchor_cache = {}

    def remember_html(self, page, document):
        """Reuse HTML already converted during the current HTML build."""
        self.anchor_cache[page.source.resolve()] = (
            page.source.stat().st_mtime_ns,
            extract_anchors(document),
        )

    def anchors_for(self, page):
        """Load actual lowdown anchors at most once per source file version."""
        source = page.source.resolve()
        stamp = page.source.stat().st_mtime_ns
        previous = self.anchor_cache.get(source)
        if previous is None or previous[0] != stamp:
            self.remember_html(page, self.load_html(page))
        return self.anchor_cache[source][1]

    def resolve(self, href, current_page):
        """Resolve a link; return ``(target_page, fragment)`` or ``None``.

        Non-document links (external URLs, images, PDFs, etc.) are intentionally
        outside the manual-reference namespace and remain unchanged. Same-page
        ``#fragment`` references are also retained as anchors, but validated.
        """
        try:
            url = urlsplit(href)
            path = unquote(url.path, errors="strict")
            fragment = unquote(url.fragment, errors="strict")
        except (UnicodeError, ValueError) as error:
            raise ReferenceError(f"{current_page.source}: invalid link {href!r}: {error}") from error

        # Schemes include https:, mailto:, and file:. //host/path is external.
        if url.scheme or url.netloc:
            return None

        if not path:
            if fragment and fragment not in self.anchors_for(current_page):
                raise ReferenceError(
                    f"{current_page.source}: unknown section #{fragment} in this page"
                )
            return None

        if not path.endswith(".md"):
            return None

        if path.startswith("/"):
            raise ReferenceError(
                f"{current_page.source}: manual link {href!r} must use a relative path"
            )
        if url.query:
            raise ReferenceError(
                f"{current_page.source}: manual link {href!r} must not have a query string"
            )

        destination = (current_page.source.parent / path).resolve()
        target = self.pages_by_path.get(destination)
        if target is None:
            raise ReferenceError(
                f"{current_page.source}: unrecognized or missing manual link {href!r} "
                f"(resolved to {destination})"
            )
        if fragment and fragment not in self.anchors_for(target):
            raise ReferenceError(
                f"{current_page.source}: unknown section #{fragment} "
                f"in {target.source}"
            )
        return target, url.fragment


class HtmlLinkRewriter(HTMLParser):
    """Rebuild HTML while changing only anchors pointing to internal .md pages.

    HTMLParser recognizes real elements rather than making unsafe substitutions
    over arbitrary HTML text. Entity references and untouched original start
    tags are retained, including the untouched lowdown ``<code>`` contents.
    """

    def __init__(self, resolver, current_page):
        super().__init__(convert_charrefs=False)
        self.resolver = resolver
        self.current_page = current_page
        self.parts = []
        self.targets = set()

    def _opening_tag(self, tag, attrs, self_closing=False):
        if tag != "a":
            return self.get_starttag_text()
        href = dict(attrs).get("href")
        if href is None:
            return self.get_starttag_text()
        resolved = self.resolver.resolve(href, self.current_page)
        if resolved is None:
            return self.get_starttag_text()

        target, fragment = resolved
        self.targets.add(target.source)
        updated_href = target.html_path.name
        if fragment:
            updated_href += f"#{fragment}"

        # Re-serialize this one element's attributes using HTML escaping.
        # The lowdown-generated text and all other elements stay untouched.
        updated = []
        for name, value in attrs:
            if name == "href":
                value = updated_href
            if value is None:
                updated.append(f" {name}")
            else:
                updated.append(f' {name}="{escape(value, quote=True)}"')
        ending = " />" if self_closing else ">"
        return f"<{tag}{''.join(updated)}{ending}"

    def handle_starttag(self, tag, attrs):
        self.parts.append(self._opening_tag(tag, attrs))

    def handle_startendtag(self, tag, attrs):
        self.parts.append(self._opening_tag(tag, attrs, self_closing=True))

    def handle_endtag(self, tag):
        self.parts.append(f"</{tag}>")

    def handle_data(self, data):
        self.parts.append(data)

    def handle_entityref(self, name):
        self.parts.append(f"&{name};")

    def handle_charref(self, name):
        self.parts.append(f"&#{name};")

    def handle_comment(self, data):
        self.parts.append(f"<!--{data}-->")

    def handle_decl(self, data):
        self.parts.append(f"<!{data}>")

    def handle_pi(self, data):
        self.parts.append(f"<?{data}>")


def rewrite_html(document, resolver, page):
    """Rewrite HTML manual links and return ``(new_html, referenced_sources)``."""
    resolver.remember_html(page, document)
    parser = HtmlLinkRewriter(resolver, page)
    parser.feed(document)
    parser.close()
    return "".join(parser.parts), parser.targets


def rewrite_mdoc(document, resolver, page):
    """Convert internal lowdown ``.Lk`` macros into semantic mdoc ``.Xr``.

    Lowdown's verified output places each link on its own macro line:

        .Lk ../concepts/storage-duration.md storage duration

    We only inspect lines starting with that macro. The first argument is the
    URL; remaining macro arguments are display text. An ``.Xr`` macro must use
    the *manual's* actual name and section, so it cannot retain an arbitrary
    Markdown link label. External ``.Lk`` macros remain verbatim.

    This is a deliberately constrained mdoc-macro transformation, not a regex
    over arbitrary HTML, Markdown, or human-readable prose.
    """
    output = []
    targets = set()
    for line in document.splitlines(keepends=True):
        if not line.startswith(".Lk "):
            output.append(line)
            continue
        components = line[4:].split(maxsplit=1)
        if not components:
            output.append(line)
            continue
        resolved = resolver.resolve(components[0], page)
        if resolved is None:
            output.append(line)
            continue
        target, _fragment = resolved
        targets.add(target.source)
        line_ending = "\n" if line.endswith("\n") else ""
        output.append(f".Xr {target.name} {target.section}{line_ending}")
    return "".join(output), targets

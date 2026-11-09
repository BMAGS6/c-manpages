#!/usr/bin/env python3
"""Build a standalone, styled HTML manual from lowdown's HTML output.

Markdown is the single source of truth. This script adds presentation only:
syntax highlighting, an automatic table of contents, and an offline-friendly
responsive layout. Read lowdown-generated standalone HTML on standard input.
"""

import argparse
import html
import json
from html.parser import HTMLParser
from pathlib import Path
from string import Template
import sys

from highlight_html import CODE_BLOCK_RE, highlight_code_block, make_stylesheet
from site_navigation import render_navigation


class DocumentParser(HTMLParser):
    """Collect document metadata and promote section headings one level.

    The page template owns the single main h1 (the manual's identifier).
    Lowdown emits Markdown # sections as h1, so these are promoted to h2;
    nested h2/h3/etc follow the same rule. Heading IDs stay untouched,
    preserving existing links to sections from other manual pages.

    Retain the raw markup of ordinary elements, including code blocks and
    links. Only actual HTML heading tags are rewritten, never code examples
    containing heading-like text.
    """

    def __init__(self):
        super().__init__(convert_charrefs=False)
        self.in_body = False
        self.in_title = False
        self.heading_level = None
        self.heading_id = None
        self.heading_text = []
        self.headings = []
        self.body_parts = []
        self.title_parts = []

    def handle_starttag(self, tag, attrs):
        if tag == "body":
            self.in_body = True
            return
        if tag == "title":
            self.in_title = True
        if not self.in_body:
            return
        if tag in ("h1", "h2", "h3", "h4", "h5", "h6"):
            level = int(tag[1])
            # HTML stops at h6. A source h6 stays h6 rather than becoming
            # an invalid h7. The canonical pages currently use h1-h3.
            promoted = min(level + 1, 6)
            start = self.get_starttag_text()
            self.body_parts.append(start.replace(f"<h{level}", f"<h{promoted}", 1))
            if self.heading_level is None:
                self.heading_level = level
                self.heading_id = dict(attrs).get("id")
                self.heading_text = []
        else:
            self.body_parts.append(self.get_starttag_text())

    def handle_startendtag(self, tag, attrs):
        if self.in_body:
            self.body_parts.append(self.get_starttag_text())

    def handle_endtag(self, tag):
        if tag == "body":
            self.in_body = False
            return
        if tag == "title":
            self.in_title = False
        if not self.in_body:
            return
        if self.heading_level is not None and tag == f"h{self.heading_level}":
            promoted = min(self.heading_level + 1, 6)
            self.body_parts.append(f"</h{promoted}>")
            if self.heading_id:
                label = html.unescape("".join(self.heading_text)).strip()
                self.headings.append((self.heading_level, self.heading_id, label))
            self.heading_level = None
            self.heading_id = None
            self.heading_text = []
        else:
            self.body_parts.append(f"</{tag}>")

    def handle_data(self, data):
        if self.in_title:
            self.title_parts.append(data)
        if self.in_body:
            self.body_parts.append(data)
            if self.heading_level is not None:
                self.heading_text.append(data)

    def handle_entityref(self, name):
        token = f"&{name};"
        if self.in_body:
            self.body_parts.append(token)
            if self.heading_level is not None:
                self.heading_text.append(token)

    def handle_charref(self, name):
        token = f"&#{name};"
        if self.in_body:
            self.body_parts.append(token)
            if self.heading_level is not None:
                self.heading_text.append(token)

    def handle_comment(self, data):
        if self.in_body:
            self.body_parts.append(f"<!--{data}-->")


def make_toc(headings):
    entries = []
    for level, anchor, label in headings:
        entries.append(
            f'<li class="toc-level-{level}"><a href="#{html.escape(anchor, quote=True)}">'
            f'{html.escape(label)}</a></li>'
        )
    return "\n".join(entries)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--template", type=Path, required=True)
    parser.add_argument("--css", type=Path, required=True)
    parser.add_argument("--page-name", help="validated manual identifier for sidebar selection")
    parser.add_argument("--section", default="7")
    parser.add_argument("--catalog", type=Path, help="generated JSON navigation catalog")
    parser.add_argument("--category", default="C language reference")
    args = parser.parse_args()

    source = sys.stdin.read()
    document = DocumentParser()
    document.feed(source)
    document.close()
    if not document.headings or not document.body_parts:
        print("error: no headings or body found in input HTML", file=sys.stderr)
        return 1

    title = html.unescape("".join(document.title_parts)).strip() or "C Manual Pages"
    # The input HTML is generated by lowdown from trusted project Markdown.
    body = "".join(document.body_parts)
    body = CODE_BLOCK_RE.sub(highlight_code_block, body)
    # The catalog is generated from the registry, not edited by hand. It is
    # optional here so build_html.py can still be used to inspect a standalone
    # lowdown conversion without first running the full site coordinator.
    catalog = json.loads(args.catalog.read_text(encoding="utf-8")) if args.catalog else []
    template = Template(args.template.read_text(encoding="utf-8"))
    rendered = template.substitute(
        title=html.escape(title),
        page_name=html.escape(title[2:] if title.startswith("c-") else title),
        manual_name=html.escape(f"{title}({args.section})"),
        category=html.escape(args.category),
        toc=make_toc(document.headings),
        navigation=render_navigation(catalog, current_page=args.page_name or title),
        body=body,
        site_css=args.css.read_text(encoding="utf-8"),
        pygments_css=make_stylesheet(),
    )
    sys.stdout.write(rendered)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

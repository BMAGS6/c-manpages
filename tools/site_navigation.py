#!/usr/bin/env python3
"""Generate deterministic navigation for the standalone C Manual Pages site.

The canonical documentation lives under ``pages/``. This module derives all
site-level links from the validated page registry built by ``build_pages.py``:
there is no hand-maintained HTML list to become stale as documents are added.

The JSON catalog is an intermediate build artifact, not authoring metadata.
It is kept outside the published HTML directory and embedded into each page
at build time. Every final HTML file is therefore still standalone and works
without JavaScript, a network connection, or any particular HTTP server.
"""

from collections import defaultdict
import html


# These are presentation labels, not a separate list of documentation pages.
# Unknown categories are derived from their directory names automatically.
CATEGORY_NAMES = {
    "keywords": "Keywords",
    "concepts": "Concepts",
}
CATEGORY_ORDER = {"keywords": 0, "concepts": 1}


def category_label(category):
    """Return a readable label for a source-directory category."""
    return CATEGORY_NAMES.get(category, category.replace("-", " ").replace("_", " ").title())


def page_href(entry):
    """Return the generated flat HTML filename for a catalog entry."""
    return f"{entry['name']}.{entry['section']}.html"


def page_label(entry):
    """Return the familiar Unix-man name displayed in site navigation."""
    return f"{entry['name']}({entry['section']})"


def make_catalog(pages, page_dir):
    """Return a stable, JSON-serializable catalog from validated pages.

    Only the first directory beneath ``pages/`` determines the category.
    Category directory names do not need to be registered in a separate file;
    additional categories automatically appear in the site index.

    All source identities have already been verified by ``discover_pages``.
    We deliberately store no filesystem-specific absolute paths in the
    catalog, so it remains stable if the project checkout is relocated.
    """
    catalog = []
    for page in pages:
        relative = page.source.relative_to(page_dir)
        category = relative.parts[0] if len(relative.parts) > 1 else "general"
        catalog.append({
            "name": page.name,
            "section": page.section,
            "category": category,
        })
    return sorted(
        catalog,
        key=lambda entry: (
            CATEGORY_ORDER.get(entry["category"], 100),
            entry["category"],
            entry["name"],
            entry["section"],
        ),
    )


def grouped_catalog(catalog):
    """Yield ordered ``(category, entries)`` pairs without changing sources."""
    groups = defaultdict(list)
    for entry in catalog:
        groups[entry["category"]].append(entry)
    return groups.items()


def render_navigation(catalog, current_page=None):
    """Render the site-wide navigation used by every generated HTML document.

    Links are relative filenames because all enhanced HTML pages live together
    in ``build/html``. The currently displayed page is marked with
    ``aria-current='page'`` for assistive technologies; it is *not* excluded
    from the navigation, so its position among other pages stays apparent.

    HTML escaping is performed at the point of use, including for attribute
    values. Catalog entries currently originate from restricted filenames,
    but escaping here provides a safe contract if metadata expands later.
    """
    parts = ['<nav class="site-nav" aria-label="Manual pages">']
    parts.append('    <h2 class="sidebar-heading">MANUAL PAGES</h2>')
    parts.append('    <a class="nav-home" href="index.html"' +
                 (' aria-current="page"' if current_page is None else '') +
                 '>Documentation home</a>')
    for group_number, (category, entries) in enumerate(grouped_catalog(catalog)):
        # Numeric group IDs cannot collide even if category directory names
        # normalize to the same text (e.g. different punctuation/spelling).
        heading_id = f"site-nav-group-{group_number}"
        parts.append(
            f'    <section class="nav-group" aria-labelledby="{html.escape(heading_id, quote=True)}">'
        )
        parts.append(
            f'        <h3 id="{html.escape(heading_id, quote=True)}" '
            f'class="nav-group-title">{html.escape(category_label(category))}</h3>'
        )
        parts.append('        <ul class="nav-page-list">')
        for entry in entries:
            selected = entry["name"] == current_page
            current = ' aria-current="page"' if selected else ''
            parts.append(
                f'            <li><a href="{html.escape(page_href(entry), quote=True)}"'
                f'{current}>{html.escape(page_label(entry))}</a></li>'
            )
        parts.append('        </ul>')
        parts.append('    </section>')
    parts.append('</nav>')
    return "\n".join(parts)


def render_index_categories(catalog):
    """Create accessible category sections for the generated homepage.

    Each link uses the same flat output name as ``render_navigation``. The
    category headings become the homepage's h2 elements below its main h1.
    """
    parts = []
    for group_number, (category, entries) in enumerate(grouped_catalog(catalog)):
        # Numeric group IDs cannot collide even if category directory names
        # normalize to the same text (e.g. different punctuation/spelling).
        heading_id = f"site-nav-group-{group_number}" + "-index"
        parts.append(
            f'<section class="index-category" aria-labelledby="{html.escape(heading_id, quote=True)}">'
        )
        parts.append(
            f'    <h2 id="{html.escape(heading_id, quote=True)}">'
            f'{html.escape(category_label(category))}</h2>'
        )
        parts.append('    <ul class="index-page-list">')
        for entry in entries:
            parts.append(
                f'        <li><a href="{html.escape(page_href(entry), quote=True)}">'
                f'<code>{html.escape(page_label(entry))}</code></a></li>'
            )
        parts.append('    </ul>')
        parts.append('</section>')
    return "\n".join(parts)

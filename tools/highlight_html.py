#!/usr/bin/env python3
"""Add static syntax highlighting to lowdown-generated standalone HTML.

Read HTML from standard input and write the highlighted document to standard
output. Only fenced code blocks with a recognized language are changed.
"""

import html
import re
import sys

from pygments import highlight
from pygments.formatters import HtmlFormatter
from pygments.lexers import get_lexer_by_name
from pygments.util import ClassNotFound


CODE_BLOCK_RE = re.compile(
    r'<pre><code class="language-([A-Za-z0-9_+#.-]+)">(.*?)</code></pre>',
    re.DOTALL,
)


def highlight_code_block(match):
    """Replace a language-tagged code block with highlighted HTML."""
    language = match.group(1)

    try:
        lexer = get_lexer_by_name(language)
    except ClassNotFound:
        return match.group(0)

    source = html.unescape(match.group(2))
    markup = highlight(source, lexer, HtmlFormatter(nowrap=True))

    return (
        f'<pre class="highlight"><code class="language-{language}">'
        f'{markup}</code></pre>'
    )


def make_stylesheet():
    """Generate self-contained light/dark syntax highlighting CSS."""
    light = HtmlFormatter(style="default")
    dark = HtmlFormatter(style="native")

    return (
        "<style>\n"
        "html { color-scheme: light dark; }\n"
        "pre.highlight { padding: 1em; overflow-x: auto; "
        "border-radius: 0.4em; tab-size: 4; }\n"
        "pre.highlight code { font-family: inherit; }\n"
        f"{light.get_style_defs('.highlight')}\n"
        "@media (prefers-color-scheme: dark) {\n"
        f"{dark.get_style_defs('.highlight')}\n"
        "}\n"
        "</style>\n"
    )


def main():
    """Highlight code blocks without modifying the rest of the document."""
    document = sys.stdin.read()
    document = CODE_BLOCK_RE.sub(highlight_code_block, document)

    head_end = re.search(r"</head\s*>", document, re.IGNORECASE)

    if head_end is None:
        print("error: expected standalone HTML with a </head> tag", file=sys.stderr)
        return 1

    document = (
        document[:head_end.start()]
        + make_stylesheet()
        + document[head_end.start():]
    )

    sys.stdout.write(document)
    return 0


if __name__ == "__main__":
    sys.exit(main())

"""Regression checks for build-time language labels on highlighted code."""

from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

from highlight_html import CODE_BLOCK_RE, highlight_code_block  # noqa: E402


class CodePresentationTests(unittest.TestCase):
    def test_highlighted_c_block_has_language_label(self):
        source = '<pre><code class="language-c">int value = 42;</code></pre>'
        rendered = CODE_BLOCK_RE.sub(highlight_code_block, source)
        self.assertIn('<pre class="highlight" data-language="c">', rendered)
        self.assertIn('class="language-c"', rendered)

    def test_unknown_language_is_left_unchanged(self):
        source = '<pre><code class="language-no_such_lexer_c_manpages">text</code></pre>'
        self.assertEqual(CODE_BLOCK_RE.sub(highlight_code_block, source), source)


if __name__ == "__main__":
    unittest.main()

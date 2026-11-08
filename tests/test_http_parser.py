"""Compile and execute the platform-independent HTTP parser stress harness.

The harness mutates known-valid request headers and feeds bounded arbitrary
byte spans to the parser. Sanitizer builds can reuse the same harness without
spinning up the socket server. Only the C standard library is needed.
"""

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent


class ParserHarnessTests(unittest.TestCase):
    """Check the parser's C99 compatibility and deterministic mutation tests."""

    def test_compiled_parser_mutations(self):
        compiler = shutil.which(os.environ.get("CC", "cc"))
        if compiler is None:
            self.skipTest("No C compiler; the native preview server is optional")

        with tempfile.TemporaryDirectory(prefix="c-manpages-parser-") as directory:
            binary = Path(directory) / ("parser-test.exe" if os.name == "nt" else "parser-test")
            args = [
                compiler, "-std=c99", "-O1", "-Wall", "-Wextra", "-Wpedantic", "-Werror",
                "-o", str(binary),
                str(ROOT / "server/http_parser.c"),
                str(ROOT / "tests/http_parser_harness.c"),
            ]
            if os.environ.get("SERVER_TEST_SANITIZE") == "1" and os.name != "nt":
                args.extend(["-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-g"])

            build = subprocess.run(args, capture_output=True, text=True, timeout=60)
            self.assertEqual(build.returncode, 0, build.stderr)
            run = subprocess.run([str(binary)], capture_output=True, text=True, timeout=20)
            self.assertEqual(run.returncode, 0, run.stderr)


if __name__ == "__main__":
    unittest.main()

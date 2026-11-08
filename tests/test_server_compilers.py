"""Build-only compiler/dialect matrix for the optional native preview server.

No interpreter plugins or CMake are required: discovered GCC and Clang
compilers are asked to compile all four ISO C modes supported by this project.
The linked binary is discarded after each case. Windows users can run this
under MinGW-w64; MSVC uses different flags and is documented separately.
"""

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent
SOURCES = [ROOT / "server" / file for file in (
    "http_server.c", "http_parser.c", "file_platform.c", "socket_platform.c",
)]


class ServerCompilerMatrixTests(unittest.TestCase):
    """Check dialect compatibility with every available GCC/Clang compiler."""

    def test_c99_c11_c17_c23_builds(self):
        # Respect CC, but still exercise GCC/Clang independently when present.
        requested = (os.environ.get("CC", "cc"), "gcc", "clang")
        compilers = {shutil.which(name) for name in requested}
        compilers.discard(None)
        if not compilers:
            self.skipTest("No native C compiler; the preview server is optional")

        with tempfile.TemporaryDirectory(prefix="c-manpages-compilers-") as directory:
            executable = Path(directory) / ("server.exe" if os.name == "nt" else "server")
            for compiler in sorted(compilers):
                for standard in ("c99", "c11", "c17", "c23"):
                    with self.subTest(compiler=compiler, standard=standard):
                        command = [
                            compiler, f"-std={standard}", "-O2", "-Wall",
                            "-Wextra", "-Wpedantic", "-Werror", "-o", str(executable),
                            *map(str, SOURCES),
                        ]
                        if os.name == "nt":
                            command.append("-lws2_32")
                        result = subprocess.run(command,
                                                capture_output=True,
                                                text=True,
                                                timeout=60)
                        # Older compilers may not implement the *optional* C23
                        # spelling. Missing support is not a source regression.
                        unsupported = "unrecognized command-line option" in result.stderr or \
                                      "invalid value" in result.stderr or \
                                      "unknown argument" in result.stderr
                        if standard == "c23" and unsupported:
                            continue
                        self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    unittest.main()

"""Exercise the native C documentation server through real loopback TCP sockets.

These integration tests compile the server into a temporary directory instead
of touching the user's build tree. They intentionally avoid HTTP client
packages: socket from Python's standard library is enough to verify protocol
behavior and reject unsafe request paths.

A missing native C compiler skips the suite because the core documentation
build itself does not require a compiler. POSIX and MinGW-w64 builds use the
same code, with Winsock linked only on Windows.
"""

import os
from pathlib import Path
import shutil
import socket
import subprocess
import tempfile
import time
import unittest


ROOT = Path(__file__).resolve().parent.parent
SOURCES = [ROOT / "server/http_server.c", ROOT / "server/socket_platform.c"]


class NativeServerTests(unittest.TestCase):
    """Launch a real C listener and make HTTP/1.1 requests to localhost."""

    @classmethod
    def setUpClass(cls):
        """Build a temporary executable and a static test documentation root."""
        compiler = shutil.which(os.environ.get("CC", "cc"))
        if compiler is None:
            raise unittest.SkipTest("No native C compiler; server is optional")

        cls.temporary = tempfile.TemporaryDirectory(prefix="c-manpages-server-")
        cls.root = Path(cls.temporary.name)
        cls.public = cls.root / "site"
        cls.public.mkdir()
        (cls.public / "c-static.7.html").write_text("<h1>static</h1>\n", encoding="utf-8")
        (cls.public / "other.css").write_text("body{}\n", encoding="utf-8")
        cls.secret = cls.root / "secret.html"
        cls.secret.write_text("NOT PUBLIC", encoding="utf-8")
        cls.executable = cls.root / ("c-manpages-serve.exe" if os.name == "nt" else "c-manpages-serve")

        command = [
            compiler,
            "-std=c17",
            "-Wall",
            "-Wextra",
            "-Wpedantic",
            "-O2",
            "-o",
            str(cls.executable),
            *map(str, SOURCES),
        ]
        if os.name == "nt":
            command.append("-lws2_32")

        result = subprocess.run(command, capture_output=True, text=True)
        if result.returncode:
            cls.temporary.cleanup()
            raise AssertionError(f"C17 compile failed:\n{result.stdout}\n{result.stderr}")

        # Choose an available port, then close the probe socket before bind.
        # A localhost port can be taken in the tiny gap; treat that as failure.
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as probe:
            probe.bind(("127.0.0.1", 0))
            cls.port = probe.getsockname()[1]

        cls.process = subprocess.Popen(
            [str(cls.executable), "--root", str(cls.public), "--port", str(cls.port)],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.PIPE,
        )

        for _ in range(100):
            if cls.process.poll() is not None:
                stderr = cls.process.stderr.read().decode(errors="replace")
                cls.temporary.cleanup()
                raise AssertionError(f"server terminated prematurely: {stderr}")
            try:
                with socket.create_connection(("127.0.0.1", cls.port), timeout=0.1):
                    break
            except OSError:
                time.sleep(0.01)
        else:
            cls.process.terminate()
            cls.process.wait(timeout=3)
            cls.temporary.cleanup()
            raise AssertionError("server never listened on loopback")

    @classmethod
    def tearDownClass(cls):
        """Stop the listener and delete the binary and temporary fixtures."""
        if hasattr(cls, "process"):
            cls.process.terminate()
            try:
                cls.process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                cls.process.kill()
                cls.process.wait(timeout=3)
            cls.process.stderr.close()
        if hasattr(cls, "temporary"):
            cls.temporary.cleanup()

    @classmethod
    def request(cls, request):
        """Send raw request bytes and collect the response until TCP closes."""
        response = bytearray()
        with socket.create_connection(("127.0.0.1", cls.port), timeout=2) as client:
            client.settimeout(3)
            client.sendall(request)
            while True:
                chunk = client.recv(4096)
                if not chunk:
                    break
                response.extend(chunk)
        headers, body = bytes(response).split(b"\r\n\r\n", maxsplit=1)
        return headers, body

    def test_get_static_document(self):
        """A successful GET returns the document and sensible HTTP headers."""
        header, body = self.request(b"GET /c-static.7.html HTTP/1.1\r\nHost: localhost\r\n\r\n")
        self.assertIn(b"200 OK", header)
        self.assertIn(b"Content-Type: text/html; charset=utf-8", header)
        self.assertIn(b"Connection: close", header)
        self.assertEqual(body, b"<h1>static</h1>\n")

    def test_root_falls_back_to_static_before_index_exists(self):
        """Phase C will add index.html; earlier builds remain easy to browse."""
        header, body = self.request(b"GET / HTTP/1.1\r\nHost: localhost\r\n\r\n")
        self.assertIn(b"200 OK", header)
        self.assertIn(b"<h1>static</h1>", body)

    def test_root_prefers_index_when_generated(self):
        """Once Phase C generates index.html, the root route discovers it."""
        index = self.public / "index.html"
        try:
            index.write_text("<h1>Index</h1>", encoding="utf-8")
            header, body = self.request(b"GET / HTTP/1.1\r\nHost: localhost\r\n\r\n")
            self.assertIn(b"200 OK", header)
            self.assertEqual(body, b"<h1>Index</h1>")
        finally:
            index.unlink(missing_ok=True)

    def test_head_returns_headers_without_body(self):
        """HEAD must report GET's content length without transmitting bytes."""
        header, body = self.request(b"HEAD /c-static.7.html HTTP/1.1\r\nHost: localhost\r\n\r\n")
        self.assertIn(b"200 OK", header)
        self.assertIn(b"Content-Length: 16", header)
        self.assertEqual(body, b"")

    def test_query_string_does_not_change_document(self):
        """A simple cache-busting query is allowed without file lookup changes."""
        header, body = self.request(b"GET /other.css?v=2 HTTP/1.1\r\nHost: localhost\r\n\r\n")
        self.assertIn(b"200 OK", header)
        self.assertEqual(body, b"body{}\n")

    def test_post_is_rejected(self):
        """The preview server is deliberately read-only."""
        header, _ = self.request(b"POST /c-static.7.html HTTP/1.1\r\nHost: localhost\r\n\r\n")
        self.assertIn(b"405 Method Not Allowed", header)
        self.assertIn(b"Allow: GET, HEAD", header)

    def test_missing_file_is_404(self):
        """Unknown but well-formed files do not become HTTP 200 responses."""
        header, _ = self.request(b"GET /missing.html HTTP/1.1\r\nHost: localhost\r\n\r\n")
        self.assertIn(b"404 Not Found", header)

    def test_path_traversal_and_encoding_are_rejected(self):
        """No slash-containing or percent-encoded filename can escape root."""
        for target in (b"/../secret.html", b"/%2e%2e%2fsecret.html", b"/sub/file.html", b"/..\\secret.html"):
            with self.subTest(target=target):
                header, body = self.request(b"GET " + target + b" HTTP/1.1\r\nHost: localhost\r\n\r\n")
                self.assertIn(b"400 Bad Request", header)
                self.assertNotIn(b"NOT PUBLIC", body)

    def test_symlink_is_not_served(self):
        """Even a flat name cannot expose a symbolic link outside docroot."""
        link = self.public / "linked.html"
        try:
            link.symlink_to(self.secret)
        except (OSError, NotImplementedError):
            self.skipTest("This platform cannot create test symlinks")
        try:
            header, body = self.request(b"GET /linked.html HTTP/1.1\r\nHost: localhost\r\n\r\n")
            self.assertIn(b"404 Not Found", header)
            self.assertNotIn(b"NOT PUBLIC", body)
        finally:
            link.unlink(missing_ok=True)

    def test_unsupported_file_type_is_forbidden(self):
        """Files with unrecognized extensions are never exposed."""
        (self.public / "config.json").write_text('{"secret": 1}', encoding="utf-8")
        header, _ = self.request(b"GET /config.json HTTP/1.1\r\nHost: localhost\r\n\r\n")
        self.assertIn(b"403 Forbidden", header)

    def test_oversized_request_is_rejected(self):
        """Cap header memory instead of allocating for attacker-selected size."""
        prefix = b"GET /c-static.7.html HTTP/1.1\r\nX-Large: "
        # Fill the exact accepted buffer without placing additional unread
        # bytes in the TCP receive queue. Closing with excess unread bytes can
        # trigger a TCP reset and obscure the error response on some kernels.
        request = prefix + b"x" * (8191 - len(prefix))
        header, _ = self.request(request)
        self.assertIn(b"431 Request Header Fields Too Large", header)


if __name__ == "__main__":
    unittest.main()

# C Manual Pages

A collection of human-oriented Unix manual pages documenting
the C programming language.

The project focuses on C language syntax, semantics, keywords,
operators, declarations, and fundamental language concepts,
rather than the C standard library.

Documentation is authored in Markdown and is built as traditional Unix manual
pages or as standalone, offline-capable HTML documentation.

## Language Versions

The primary target is C23, with historical coverage of earlier
standards and future consideration of C29.

## Status

Under development. The first reference page documents `static`.

## Building the documentation

Build dependencies: POSIX-compatible `make`, Python 3, `lowdown`, and `mandoc`.
The enhanced HTML output also requires Python's `pygments` module; `man` is
needed to open pages using `make view`.

From the repository root, run:

```sh
make doctor                   # Check tools and show discovered documents.
make                          # Build every Unix mdoc page.
make check                    # Build and validate all mdoc pages.
make html                     # Build every standalone HTML page.
make list-pages               # Show page identifiers and Markdown sources.
make view                     # Open c-static(7) in the local manual viewer.
make view PAGE=c-static       # Explicitly select the page to open.
make test                     # Run discovery and incremental-build tests.
make clean                    # Remove generated files.
```

Optional comparison targets include `make lint-strict`, `make html-mandoc`, and
`make html-highlighted`.

Generated pages are written into `build/man7/` and `build/html/`, both ignored
by Git. Incremental builds check timestamps of source documents and relevant
build inputs. If a page is **renamed or deleted**, use `make clean` to remove
its old generated outputs.

## Adding a manual page

Create a `.md` file anywhere beneath `pages/`. For example, create
`pages/concepts/storage-duration.md` beginning with the established lowdown
metadata preamble (including the terminating blank line):

```text
title: c-storage-duration
section: 7
source: C Manual Pages

# NAME

c-storage-duration -- storage durations of C objects
```

Then run `make` and `make html`. No Makefile edits are required. The new
outputs will be `build/man7/c-storage-duration.7` and
`build/html/c-storage-duration.7.html`.

Source filenames must be lowercase and hyphen-separated. Generated manual names
must be unique across all categories. The build stops on invalid metadata,
name collisions, or a mismatch between the source filename and its title.

## Cross-references between manual pages

Write links between the canonical Markdown documents using ordinary relative
Markdown syntax. **Do not** link to generated HTML output from a source page:

```markdown
For more information, see
[storage duration](../concepts/storage-duration.md).
```

The build automatically converts this to a link to
`c-storage-duration.7.html` in HTML, and a semantic `.Xr c-storage-duration 7`
reference in Unix mdoc. The rendered mdoc reference displays the **manual
name**, rather than an arbitrary Markdown link label. Normal web URLs, other
files, and source Markdown remain unchanged.

The target Markdown page **must already exist** when a link is added. A missing
`.md` page or invalid section fragment causes `make`/`make html` to fail with a
clear error instead of silently producing a broken link. For unwritten pages,
keep a plain textual reference such as `` `c-linkage(7)` `` until the document
has been created.

HTML cross-page fragments such as
`[static objects](../concepts/storage-duration.md#static-storage-duration)`
are preserved and checked against lowdown's actual generated HTML element IDs.
In terminal manuals, `.Xr` references the entire target manual; it cannot
identify a section within that manual. The fragment is still checked.

The build records discovered link dependencies in `build/references/`. Editing a
linked page causes its referring pages to be revalidated. As before, run
`make clean` after **deleting or renaming** a documentation page to remove
orphaned generated files.

Site-wide navigation and a homepage are planned for the next milestone.

## Serve the generated HTML with any HTTP server

**The documentation is plain static HTML, independent of the preview server.**
`make`, `make check`, and `make html` do **not** compile or run the bundled C
server. The native server is only an optional development convenience, and
it is not needed to host the generated site.

Build the HTML first:

```sh
make html
```

Then point **any** static-file HTTP server at `build/html/`. This includes
nginx, Apache httpd, Caddy, Python's `http.server`, and other implementations.
You may also copy the contents of `build/html/` to a static hosting provider.
No Python-based *web server* or C compiler is needed for hosting (although
Python remains part of the current **HTML build** process).

Opening an HTML file directly with `xdg-open` can involve the XDG document
portal, which sometimes prevents a browser from resolving neighboring files.
A local HTTP server avoids that issue.

### Optional preview commands

The Makefile offers several convenience modes on POSIX systems:

```sh
make serve                          # Build HTML and run the bundled C server.
make serve PORT=8080                # Select its listening port.
make serve BACKEND=python           # Use Python's standard-library server.
make serve BACKEND=external \
    SERVER_COMMAND='busybox httpd -f -p 127.0.0.1:8000 -h build/html'
```

The BusyBox command is merely an **example**; it requires BusyBox to be
installed. Any other command is acceptable if it serves `build/html/`.
`SERVER_COMMAND` is a trusted, user-supplied shell command executed from the
repository root; the Makefile does not interpret or append `PORT` to it.
Configure the address, port, and document root using your chosen server's
own arguments. Bind to `127.0.0.1` for local preview unless you deliberately
intend to expose the site on a network.

The `serve` target is only a shortcut. You can bypass it entirely:

```sh
make html
python3 -m http.server 8000 --bind 127.0.0.1 --directory build/html
```

Preview processes run in the foreground; **Ctrl+C** stops them. Until Phase C
generates `index.html`, the bundled C server maps `/` to `c-static.7.html`.
Other HTTP servers may list the directory or return 404 at `/`; in that case,
visit `http://127.0.0.1:8000/c-static.7.html` explicitly.

The bundled native server is intentionally tiny, single-threaded, blocking,
and limited to local development. It does not implement a general-purpose web
server, and no documentation output depends on its particular behavior.

The C standard is selected by the **compiler** using `-std=$(CSTD)`. Source
`#if __STDC_VERSION__` checks permit optional newer-language code, but do not
change a compiler's language mode. ISO C99 is the minimum language standard;
C11, C17, and C23 are supported when the compiler offers those modes. An
experimental C29 mode is not promised until compilers actually support one.
This is independent of platform APIs: POSIX sockets and Windows Winsock2 are
not part of ISO C. Microsoft's MSVC does not offer a C99 compilation mode;
Windows builds with MinGW-w64 can use C99, while MSVC requires separate build
instructions and a newer mode.

### Windows (MinGW-w64 / MSYS2)

Run from an MSYS2 UCRT64 or similar shell that provides `make`, GCC, Python,
lowdown, and the project's normal HTML build dependencies:

```sh
make serve EXE=.exe SERVER_LIBS=-lws2_32
```

The server sources are also compatible with Winsock2 and can be compiled with
an appropriate Windows C compiler. The POSIX make invocation is tested on
POSIX; run the Windows recipe on Windows to validate your toolchain.

### Resource use and protocol boundaries

The native server uses **blocking** sockets. The OS puts the process to sleep
inside `accept()` when no browser is connecting; there is **no busy-waiting**,
thread pool, or periodic timer. Active connections have 5-second socket I/O
timeouts, plus an independent **5-second total deadline for receiving the HTTP
request headers**. The latter prevents a client from keeping the single worker
occupied indefinitely by transmitting a byte before each socket timeout.
A monotonic clock is used so changing the system clock cannot extend this
header deadline. File response sends remain bounded *per socket operation*;
a full-response deadline and concurrency are not part of this small server.

Supported requests are deliberately narrower than a general HTTP daemon:

- HTTP/1.0 or HTTP/1.1, **GET and HEAD only**, one request per connection.
- HTTP/1.1 requires exactly one syntactically valid `Host` field; HTTP header
  field names are case-insensitive. Extra whitespace in the request line,
  obsolete folded headers, NULs and other invalid control bytes are rejected.
- Request-body framing (`Content-Length`, `Transfer-Encoding`) is rejected,
  including on GET. The server neither parses uploads nor handles pipelining.
- Only *origin-form* paths, a **single ASCII filename**, and optional query
  strings are accepted; percent escapes and directory separators are rejected.
  The server is not an RFC-complete HTTP implementation or a reverse proxy.
- The root URL `/` prefers `index.html` and falls back to `c-static.7.html`
  until the site index exists. An explicit `/index.html` request does not fall
  back. Missing files return 404, unsupported file extensions return 403.

These rules follow a strict subset of the HTTP/1.1 message syntax described
by [RFC 9112](https://www.rfc-editor.org/rfc/rfc9112.html). The fixed request
buffer is 8 KiB. A request exceeding it produces HTTP 431; an incomplete
request or malformed header produces HTTP 400, and an expired receive deadline
produces HTTP 408. The standard permits some tolerance for legacy whitespace;
this developer-only implementation intentionally rejects it instead.

### Filesystem and error handling

The document root is validated **before** the TCP listener starts. It must
exist and be a real, accessible directory. File names are checked before any
file lookup. Only known MIME extensions and ordinary files can be served.
Symbolic links/reparse points, directories, named pipes, and other special
files are not served.

The file is opened once, then its metadata and bytes are read using the **same
handle**, removing the previous time-of-check/time-of-use gap for the leaf
file. On POSIX, the server holds an open root directory descriptor and uses
`openat()` with `O_NOFOLLOW`, `O_NONBLOCK`, and `fstat()`. On Windows it uses
`CreateFileA()` with `FILE_FLAG_OPEN_REPARSE_POINT`, validates the opened
handle, and reads from that handle. The document root remains trusted: users
who can replace or modify its contents (or rearrange the Windows root path)
should not be considered adversaries. Windows `CreateFileA` uses the Windows
ANSI code page, so a non-ASCII custom root path is not yet supported reliably.

File size is captured before response headers are sent. The first read is
attempted before sending HTTP 200; subsequent reads and sends are checked for
errors or truncation. Since status headers cannot be recalled after sending,
logs distinguish the **HTTP status** from an **incomplete transfer**. The
POSIX and Winsock adapters save errors before socket cleanup so original
failure diagnostics are not replaced by errors from `close()`.

This is a **localhost-only preview utility**, not a hardened internet server.
It always binds to `127.0.0.1`. It does not provide TLS, authentication,
CGI, uploads, HTTP/2, HTTP/3, persistent connections, or public access.

### Testing the native server

`make test` also runs the C99 HTTP parser mutation harness and an optional
GCC/Clang matrix for C99, C11, C17 and C23 (when the installed toolchain
understands those language switches). The live socket tests cover normal GET
and HEAD, malformed requests, duplicate or missing Host, traversal attempts,
symlinks, directories and FIFOs, large files, and slow clients.

```sh
make test
make -B server CSTD=c99 CFLAGS='-O2 -Wall -Wextra -Wpedantic -Werror'
make server CSTD=c23
make test-server-sanitize   # Optional: GCC/Clang with ASan + UBSan on POSIX.
```

The sanitizer target is opt-in and requires a compatible compiler and its
sanitizer runtime. Compiler flags are not tracked as Make dependencies, hence
`-B` is necessary to force a new binary when only `CFLAGS` changes.

Windows builds are supported by the source's Winsock and Win32 branches, but
must be **compiled and exercised on Windows** before claiming Windows
integration verification. The provided test suite uses MinGW-w64/GCC-style
flags. MSVC requires its own C11/C17 build command and does not accept
`-std=c99`. For Windows with MinGW-w64:

```sh
make server CSTD=c99 EXE=.exe SERVER_LIBS=-lws2_32
make test
```

For further development, fuzzing the isolated `http_parser.c` implementation,
Windows CI, transfer-deadline tests, and measuring idle CPU/latency under real
browser workloads are sensible next steps. They are not prerequisites for
using the preview server today.

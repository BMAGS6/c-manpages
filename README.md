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

## Preview the HTML using the optional C server

Opening a single HTML page through `xdg-open` can put it behind the desktop's
XDG document portal, causing links to neighboring HTML files to fail. Instead,
the repository includes an intentionally tiny, **blocking**, local-only HTTP
preview server written in ISO C17-compatible code. It uses no third-party
HTTP library and works with POSIX sockets or Windows Winsock2.

On a POSIX system, from the repository root:

```sh
make serve                         # Build HTML, compile C server, serve locally.
make serve PORT=8080               # Pick another local port.
make server CSTD=c23               # Compile the optional server as C23.
make serve CSTD=c23 PORT=8080      # Use that version for the preview.
make serve BACKEND=python          # Alternative: Python standard-library server.
```

Then open `http://127.0.0.1:8000/` (or the requested port). The server runs in
that terminal; **Ctrl+C** stops it. Until the generated site has an `index.html`,
the root URL opens `c-static.7.html`.

The C standard is selected by the **compiler** using `-std=$(CSTD)`. Source
`#if __STDC_VERSION__` checks permit optional newer-language code, but do not
change a compiler's language mode. C17 is the lowest supported standard; C23
is supported with a sufficiently recent compiler. An experimental C29 mode is
not promised until compilers actually support one.

### Windows (MinGW-w64 / MSYS2)

Run from an MSYS2 UCRT64 or similar shell that provides `make`, GCC, Python,
lowdown, and the project's normal HTML build dependencies:

```sh
make serve EXE=.exe SERVER_LIBS=-lws2_32
```

The server sources are also compatible with Winsock2 and can be compiled with
an appropriate Windows C compiler. The POSIX make invocation is tested on
POSIX; run the Windows recipe on Windows to validate your toolchain.

### Resource use and security limits

The server **does not periodically poll** for new requests. It blocks in the
OS's `accept()` syscall when idle, and uses blocking `recv()` / `send()` with
five-second socket timeouts for active clients. It is single-threaded, serves
GET/HEAD only, sends `Connection: close`, and allocates no per-client worker
threads. Memory usage is constant apart from C library/socket internals.

It always binds **only to 127.0.0.1**. Paths must be single ASCII filenames:
no directories, encoded URLs, `..` path components or directory listings are
allowed. Symbolic links/reparse points and unknown file types are declined.
The source directory is assumed to contain trusted, generated documentation.
This is not a hardened server for use on untrusted networks or for hosting
arbitrary third-party files; it does not implement TLS or concurrent clients.

Test the server separately with:

```sh
python3 -m unittest discover -s tests -p 'test_http_server.py' -v
```

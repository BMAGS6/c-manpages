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

**Not implemented in Phase A:** rewriting relative Markdown links to generated
HTML links and `.Xr` references, or generating site-wide navigation. Those are
the subsequent milestones; regular text references remain unchanged for now.

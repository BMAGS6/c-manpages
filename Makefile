.POSIX:

# C Manual Pages -- POSIX make entry points for all Markdown documentation.
#
# POSIX make cannot recursively enumerate source files without nonportable
# extensions. tools/build_pages.py handles discovery, collision detection,
# per-file dependency freshness, and the existing converter invocations.
# Targets here are intentionally phony; Python skips unchanged outputs.

LOWDOWN = lowdown
MANDOC = mandoc
MAN = man
PYTHON = python3
PAGE_BUILDER = tools/build_pages.py
PAGE = c-static

# Optional local preview server. CC is the native C compiler; choose a dialect
# with CSTD=c99 (portable baseline), CSTD=c17, or CSTD=c23. The standard
# becomes part of the output path so different dialects do not collide.
#
# On MinGW/MSYS2 use EXE=.exe SERVER_LIBS=-lws2_32.  The server is entirely
# optional: building and installing documentation does not require a compiler.
# Set the native compiler explicitly. With .POSIX:, some make implementations
# default CC to the C99-specific 'c99' driver, which rejects -std=c17/c23.
CC = cc
CSTD = c99
SERVER_CFLAGS = -O2 -Wall -Wextra -Wpedantic -std=$(CSTD)
SERVER_LIBS =
EXE =
SERVER_EXE = build/bin/$(CSTD)/c-manpages-serve$(EXE)
SERVER_SOURCES = server/http_server.c server/http_parser.c server/file_platform.c server/socket_platform.c
PORT = 8000
BACKEND = c

# Set these on the command line when needed, e.g. make view PAGE=c-static.
BUILDER_OPTIONS = --lowdown "$(LOWDOWN)" --mandoc "$(MANDOC)" --man "$(MAN)"

.PHONY: all doctor man lint lint-strict check html html-highlighted html-mandoc view list-pages test test-server-sanitize server serve clean

all: man

man:
	$(PYTHON) "$(PAGE_BUILDER)" $(BUILDER_OPTIONS) man

html:
	$(PYTHON) "$(PAGE_BUILDER)" $(BUILDER_OPTIONS) html

lint:
	$(PYTHON) "$(PAGE_BUILDER)" $(BUILDER_OPTIONS) lint

lint-strict:
	$(PYTHON) "$(PAGE_BUILDER)" $(BUILDER_OPTIONS) lint-strict

check: lint

# Preserve optional output formats for comparison and regression checking.
html-highlighted:
	$(PYTHON) "$(PAGE_BUILDER)" $(BUILDER_OPTIONS) html-highlighted

html-mandoc:
	$(PYTHON) "$(PAGE_BUILDER)" $(BUILDER_OPTIONS) html-mandoc

view:
	$(PYTHON) "$(PAGE_BUILDER)" $(BUILDER_OPTIONS) --page "$(PAGE)" view

list-pages:
	$(PYTHON) "$(PAGE_BUILDER)" list

# These tests need only Python's standard library and Pygments (for HTML).
test:
	$(PYTHON) -m unittest discover -s tests -v

# Optional GCC/Clang debugging run: compile both the pure parser and the
# running server under AddressSanitizer + UndefinedBehaviorSanitizer. This is
# intentionally opt-in because sanitizer runtimes are compiler-dependent.
test-server-sanitize:
	SERVER_TEST_SANITIZE=1 $(PYTHON) -m unittest discover -s tests -p 'test_http_*.py' -v

doctor:
	@for tool in "$(LOWDOWN)" "$(MANDOC)" "$(MAN)" "$(PYTHON)"; do \
		if command -v "$$tool" >/dev/null 2>&1; then \
			printf '[OK] %s: %s\n' "$$tool" "$$(command -v "$$tool")"; \
		else \
			printf '[MISSING] %s\n' "$$tool" >&2; \
			exit 1; \
		fi; \
	done
	@$(PYTHON) -c 'import pygments' >/dev/null 2>&1 && \
		printf '[OK] Pygments (Python module)\n' || { \
		printf '[MISSING] Pygments (Python module)\n' >&2; \
		exit 1; \
	}
	@$(PYTHON) "$(PAGE_BUILDER)" list

# Build a tiny C99+ localhost-only web server. Only this optional target
# requires a native C compiler or Winsock link library.
$(SERVER_EXE): $(SERVER_SOURCES) server/socket_platform.h server/http_parser.h server/file_platform.h
	mkdir -p "build/bin/$(CSTD)"
	$(CC) $(CPPFLAGS) $(SERVER_CFLAGS) $(CFLAGS) -o "$@" $(SERVER_SOURCES) $(LDFLAGS) $(SERVER_LIBS)

server: $(SERVER_EXE)

# Allow a familiar Python server as an opt-in alternative. This recipe runs the
# HTTP server in the foreground; Ctrl+C stops it. The build remains POSIX make.
serve: html
	@case "$(BACKEND)" in \
		c) $(MAKE) server && "./$(SERVER_EXE)" --root build/html --port "$(PORT)" ;; \
		python) $(PYTHON) -m http.server "$(PORT)" --bind 127.0.0.1 --directory build/html ;; \
		*) echo "Unknown BACKEND='$(BACKEND)' (choose c or python)" >&2; exit 1 ;; \
	esac

clean:
	rm -rf build

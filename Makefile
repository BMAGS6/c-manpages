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

# Set these on the command line when needed, e.g. make view PAGE=c-static.
BUILDER_OPTIONS = --lowdown "$(LOWDOWN)" --mandoc "$(MANDOC)" --man "$(MAN)"

.PHONY: all doctor man lint lint-strict check html html-highlighted html-mandoc view list-pages test clean

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

clean:
	rm -rf build

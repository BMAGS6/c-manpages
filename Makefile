.POSIX:

# C Manual Pages: first-stage build for c-static(7).
# POSIX make syntax; lowdown and Pygments are build dependencies.

LOWDOWN = lowdown
MANDOC = mandoc
MAN = man
PYTHON = python3

SOURCE = pages/keywords/static.md
MANPAGE = build/man7/c-static.7
HTMLPAGE = build/html/c-static.7.html
HIGHLIGHTED_HTML = build/html/c-static-highlighted.html
HIGHLIGHTER = tools/highlight_html.py

.PHONY: all doctor man lint lint-strict check html html-highlighted view clean

all: $(MANPAGE)

man: $(MANPAGE)

$(MANPAGE): $(SOURCE)
	@mkdir -p build/man7
	$(LOWDOWN) -s -t mdoc --roff-manpage "$(SOURCE)" > "$@.tmp" && mv "$@.tmp" "$@"

lint: $(MANPAGE)
	$(MANDOC) -W error -T utf8 "$(MANPAGE)" > /dev/null

lint-strict: $(MANPAGE)
	$(MANDOC) -T lint "$(MANPAGE)"

check: lint

html: $(HTMLPAGE)

$(HTMLPAGE): $(MANPAGE)
	@mkdir -p build/html
	$(MANDOC) -T html -O 'man=%N.%S.html' "$(MANPAGE)" > "$@.tmp" && mv "$@.tmp" "$@"

html-highlighted: $(HIGHLIGHTED_HTML)

$(HIGHLIGHTED_HTML): $(SOURCE) $(HIGHLIGHTER)
	@mkdir -p build/html
	$(LOWDOWN) -s -t html "$(SOURCE)" > "$@.raw.tmp"
	$(PYTHON) "$(HIGHLIGHTER)" < "$@.raw.tmp" > "$@.tmp" && mv "$@.tmp" "$@"
	@rm -f "$@.raw.tmp"

view: $(MANPAGE)
	$(MAN) -l "$(MANPAGE)"

doctor:
	@for tool in lowdown mandoc man python3; do \
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

clean:
	rm -rf build

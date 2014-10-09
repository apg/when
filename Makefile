CFLAGS = -Wall -pedantic -Wno-parentheses
PREFIX ?= /usr/local
MANPATH ?= $(PREFIX)/share/man

all: when retry

when: when.c
	$(CC) $< $(CFLAGS) -o $@

retry: retry.c
	$(CC) $< $(CFLAGS) -o $@

README: when.1
	man -P cat ./when.1 | fmt -w 79 > $@

install: when retry
	install -m 0755 when $(PREFIX)/bin/when
	install -m 0644 when.1 $(MANPATH)/man1/when.1
	install -m 0755 retry $(PREFIX)/bin/retry

uninstall:
	rm -f $(PREFIX)/bin/when
	rm -f $(MANPATH)/man1/when.1
	rm -f $(PREFIX)/bin/retry

clean:
	rm -f when retry

.PHONY: all clean install uninstall README

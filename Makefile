CFLAGS = -Wall -pedantic -Wno-parentheses
PREFIX ?= /usr/local
MANPATH ?= $(PREFIX)/share/man/

all: when

when: when.c
	$(CC) $< $(CFLAGS) -o $@

README: when.1
	man -P cat ./when.1 > $@

install: when
	install -m 0755 when $(PREFIX)/bin/when
	install -m 0644 when.1 $(MANPATH)/man1/when.1

uninstall:
	rm -f $(PREFIX)/bin/when
	rm -f $(MANPATH)/man1/when.1

clean:
	rm -f when

.PHONY: all clean install uninstall README

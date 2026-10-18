CC	?= cc
PREFIX	?= /usr/local
DESTDIR	?=
BINDIR	?= $(PREFIX)/bin
DATADIR	?= $(PREFIX)/share/wordle

ifndef CURSES_LIBS
CURSES_CFLAGS := $(shell pkg-config --cflags ncurses 2>/dev/null)
CURSES_LIBS   := $(shell pkg-config --libs ncurses 2>/dev/null || echo -lncurses)
endif

CFLAGS	?= -O2 -g
WARN	= -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wstrict-prototypes \
	  -Wformat=2 -Wcast-qual -Wwrite-strings
override CFLAGS	+= -std=c17 $(WARN)
override CPPFLAGS += -D_POSIX_C_SOURCE=200809L -DWORDLE_DATADIR='"$(DATADIR)"' \
	  $(CURSES_CFLAGS) -MMD -MP

OBJS	= src/main.o src/game.o src/dict.o src/rng.o src/input.o src/ui.o
TOBJS	= tests/test_wordle.o src/game.o src/dict.o src/rng.o

all: wordle

wordle: $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(CURSES_LIBS) $(LDLIBS)

tests/test_wordle: $(TOBJS)
	$(CC) $(LDFLAGS) -o $@ $(TOBJS) $(LDLIBS)

test: tests/test_wordle
	./tests/test_wordle

run: wordle
	./wordle

install: wordle
	install -d $(DESTDIR)$(BINDIR) $(DESTDIR)$(DATADIR)
	install -m 755 wordle $(DESTDIR)$(BINDIR)/wordle
	install -m 644 data/words.txt $(DESTDIR)$(DATADIR)/words.txt

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/wordle $(DESTDIR)$(DATADIR)/words.txt
	-rmdir $(DESTDIR)$(DATADIR)

clean:
	rm -f wordle tests/test_wordle src/*.o src/*.d tests/*.o tests/*.d

.PHONY: all test run install uninstall clean

-include $(wildcard src/*.d tests/*.d)

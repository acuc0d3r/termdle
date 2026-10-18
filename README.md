# termdle

A Wordle clone for the Linux terminal. C17, ncurses, no network, no other
dependencies. Pick a word length from 3 to 12 letters, guess the word.

## Dependencies

A C compiler, `make`, and the ncurses development files (`pkg-config` is
optional; without it the Makefile falls back to `-lncurses`).

    Debian/Ubuntu   sudo apt install build-essential libncurses-dev
    Fedora/RHEL     sudo dnf install gcc make ncurses-devel
    Arch/Artix      sudo pacman -S base-devel ncurses
    Alpine          apk add build-base ncurses-dev

## Build and run

    make            # builds ./wordle
    make run        # build and play
    make test       # unit tests (no ncurses needed to run them)
    sudo make install               # /usr/local/bin + /usr/local/share/wordle
    make install PREFIX=$HOME/.local
    make install DESTDIR=/tmp/pkg PREFIX=/usr     # for packagers
    make uninstall
    make clean

The build uses `-std=c17 -Wall -Wextra -Wpedantic -Wshadow -Wconversion ...`
and is warning-free. Add `CFLAGS="-O2 -Werror"` to make it fatal.

## Controls

Menu:

    Up/Down (k/j)       select item
    Left/Right (h/l)    change word length (wraps 3..12; also - and +);
                        lengths the word list has no words for are skipped
    Enter               choose; on "Length" it steps forward
    ?                   instructions
    q or Esc            quit

Game:

    A-Z                 type a letter
    Backspace           delete
    Enter               submit the guess
    Esc                 back to the menu (abandons the game)
    Ctrl-C              quit

After a game: Enter or Space plays again with the same length, Esc or `m`
goes to the menu to pick another length, `q` quits.

## Rules

Green: right letter, right place. Yellow: letter is in the word, wrong
place. Gray: not in the word. Duplicates follow the official behaviour:
greens are assigned first, then a non-green letter is yellow only while
the answer still has an unmatched copy of it. So guessing `lolly` against
`hello` marks only the two `l`s that are in the right place.

Guesses allowed: `length + 1`, never fewer than 6 (so 3-5 letters get 6,
12 letters get 13). Override with `-t N`. A guess must be a word from the
dictionary. The answer is always drawn from the same dictionary.

The keyboard at the bottom shows the best known state of each letter
(green beats yellow beats gray). Terminals that are too small for the
grid show a notice with the required size; resizing redraws immediately.
The layout picks the largest tile style that fits, down to 16 rows for six
guesses, and 80x24 is enough for every length.

A submitted guess flips tile by tile (about 0.7 s for a whole row, however
long the word) and a refused guess shakes the row. Keys pressed during an
animation are discarded. `-n` turns both off, which is kinder over a slow
link.

## Configuration

    wordle [-d dict] [-l length] [-t tries] [-s seed] [-n]

    -d dict    word list to use
    -l length  start with this length selected (3-12); must exist in the list
    -t tries   fixed number of guesses, 1-16
    -s seed    fixed seed: same seed, same sequence of answers
    -n         no animations
    -h         usage

Without `-s` the generator is seeded from `/dev/urandom` (falling back to
time and pid). Answers are picked with rejection sampling, so every word
of the chosen length is exactly equally likely.

### Dictionary

One word per line, ASCII letters only, 3 to 12 letters. Case is folded,
duplicates removed, `#` comment lines, blank lines and anything else
(digits, accents, spaces inside a word) are skipped silently. Add or
remove words freely; `data/words.txt` ships with about 3800 common English
words, so some rare-but-valid guesses will be rejected. Drop in a larger
list with `-d` or `WORDLE_DICT` if you want. `/usr/share/dict/words` works.

The word list is searched in this order, first readable file wins:

1. `-d FILE`, or if absent `$WORDLE_DICT` (used exclusively, never
   second-guessed: if it is unreadable you get an error naming it)
2. `<dir of the binary>/../share/wordle/words.txt` (relocatable installs)
3. `$(DATADIR)/words.txt`, compiled in (`/usr/local/share/wordle` by default)
4. `<dir of the binary>/data/words.txt` (running from the source tree)
5. `./data/words.txt`

Change the compiled-in location with `make DATADIR=/some/where`.

## Layout

    Makefile
    README.md
    data/words.txt      dictionary
    src/main.c          option parsing, menu / help / play state machine
    src/game.[ch]       scoring, guess budget, game state (no I/O)
    src/dict.[ch]       loading, filtering by length, lookup, random pick
    src/rng.[ch]        splitmix64 + unbiased range selection
    src/input.[ch]      curses keys -> abstract events, signals
    src/ui.[ch]         all drawing
    tests/test_wordle.c scoring, length validation, dictionary, rng

## Troubleshooting

`curses.h: No such file or directory`: install the ncurses development
package (see Dependencies).

`cannot read word list; tried: ...`: the data file is not where the
binary looks. Run from the source tree, run `make install`, or pass
`-d /path/to/words.txt`.

`no N-letter words in FILE (available: ...)`: the list has no words of
that length. The menu shows the lengths available and refuses to start an
empty one.

`cannot initialise terminal`: `$TERM` is unset or has no terminfo entry.
Try `TERM=xterm-256color wordle`.

`stdin and stdout must be a terminal`: do not pipe or redirect the game.

Colours look wrong: with 256-colour terminfo you get green, amber and gray
tiles; with 8 colours the gray tile is black-on-white; with no colour at
all, green is reverse video, yellow is underlined, gray is dim. Only tiles
and a few highlights are coloured; the background is always your own.

Escape feels slow in some terminals or over ssh: the Esc delay is 25 ms.
Terminals that send arrow keys as Esc sequences slowly may need `ESCDELAY`
raised in `ui_init()`.

Terminal left in a bad state after a crash: run `reset`.

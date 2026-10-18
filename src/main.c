#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "dict.h"
#include "game.h"
#include "input.h"
#include "rng.h"
#include "ui.h"

enum { ST_MENU, ST_HELP, ST_PLAY, ST_QUIT };

static void usage(FILE *f)
{
	fprintf(f, "usage: wordle [-d dict] [-l length] [-t tries] [-s seed] [-n]\n"
	    "  -d dict    word list (default: search; $WORDLE_DICT also works)\n"
	    "  -l length  preselect the word length (%d-%d)\n"
	    "  -t tries   fixed number of guesses (1-%d; default length+1, min 6)\n"
	    "  -s seed    fixed random seed, for reproducible games\n"
	    "  -n         no animations (tile flip, shake)\n"
	    "  -h         this text\n", MIN_LEN, MAX_LEN, MAX_TRIES);
}

static int num(const char *s, long lo, long hi, long *out)
{
	char *e;
	long v;

	errno = 0;
	v = strtol(s, &e, 10);
	if (errno || e == s || *e || v < lo || v > hi)
		return -1;
	*out = v;
	return 0;
}

/*
 * Move the chosen length one step, wrapping, and on past any length the
 * dictionary has no words for: the menu shows those faded, and stopping
 * on them would only lead to a refusal at "Play".
 */
static void
step(const struct dict *d, int *len, int dir)
{
	int i;

	for (i = MIN_LEN; i <= MAX_LEN; i++) {
		*len += dir;
		if (*len > MAX_LEN)
			*len = MIN_LEN;
		if (*len < MIN_LEN)
			*len = MAX_LEN;
		if (dict_count(d, *len) > 0)
			return;
	}
}

static int menu(const struct dict *d, int *len, int tries)
{
	int sel = M_START;
	char msg[80] = "";
	struct event e;

	for (;;) {
		if (ui_menu(sel, *len, tries, d, msg) < 0)
			return ST_QUIT;
		e = input_get();
		msg[0] = '\0';
		switch (e.type) {
		case EV_QUIT: case EV_ESC:
			return ST_QUIT;
		case EV_UP:
			sel = (sel + M_N - 1) % M_N;
			break;
		case EV_DOWN:
			sel = (sel + 1) % M_N;
			break;
		case EV_LEFT:
			step(d, len, -1);
			break;
		case EV_RIGHT:
			step(d, len, 1);
			break;
		case EV_ENTER:
			if (sel == M_LEN) {
				step(d, len, 1);
			} else if (sel == M_START) {
				if (dict_count(d, *len) == 0)
					snprintf(msg, sizeof msg,
					    "No %d-letter words in the dictionary", *len);
				else
					return ST_PLAY;
			} else if (sel == M_HELP) {
				return ST_HELP;
			} else {
				return ST_QUIT;
			}
			break;
		case EV_CHAR:
			switch (e.ch) {
			case 'q': return ST_QUIT;
			case '?': return ST_HELP;
			case 'k': sel = (sel + M_N - 1) % M_N; break;
			case 'j': sel = (sel + 1) % M_N; break;
			case 'h': case '-': step(d, len, -1); break;
			case 'l': case '+': case '=': step(d, len, 1); break;
			}
			break;
		}
	}
}

static int help(void)
{
	struct event e;

	for (;;) {
		if (ui_help() < 0)
			return ST_QUIT;
		e = input_get();
		if (e.type == EV_QUIT)
			return ST_QUIT;
		if (e.type != EV_NONE && e.type != EV_RESIZE)
			return ST_MENU;
	}
}

static int play(const struct dict *d, int len, int tries, struct rng *r)
{
	struct game g;
	struct event e;
	char cur[MAX_LEN + 1] = "", msg[80] = "";
	const char *ans = dict_pick(d, len, r);
	int ncur = 0;

	if (ans == NULL || game_init(&g, ans, tries ? tries : game_tries(len)) < 0)
		return ST_MENU;
	for (;;) {
		if (ui_game(&g, cur, ncur, msg) < 0)
			return ST_QUIT;
		e = input_get();
		msg[0] = '\0';
		if (e.type == EV_QUIT)
			return ST_QUIT;
		if (g.state != G_PLAY) {
			if (e.type == EV_ENTER || (e.type == EV_CHAR && e.ch == ' '))
				return ST_PLAY;
			if (e.type == EV_ESC || (e.type == EV_CHAR && e.ch == 'm'))
				return ST_MENU;
			if (e.type == EV_CHAR && e.ch == 'q')
				return ST_QUIT;
			continue;
		}
		switch (e.type) {
		case EV_ESC:
			return ST_MENU;
		case EV_BACK:
			if (ncur > 0)
				cur[--ncur] = '\0';
			break;
		case EV_CHAR:
			if (e.ch >= 'a' && e.ch <= 'z' && ncur < len) {
				cur[ncur++] = (char)e.ch;
				cur[ncur] = '\0';
			}
			break;
		case EV_ENTER:
			if (ncur < len) {
				snprintf(msg, sizeof msg, "Not enough letters");
			} else if (!dict_has(d, cur, len)) {
				snprintf(msg, sizeof msg, "Not in word list");
			} else {
				game_submit(&g, cur);
				ncur = 0;
				cur[0] = '\0';
				if (ui_reveal(&g, g.nguess - 1) < 0)
					return ST_QUIT;
				break;
			}
			if (ui_shake(&g, cur, ncur, msg) < 0)
				return ST_QUIT;
			break;
		}
	}
}

int main(int argc, char **argv)
{
	char path[PATH_MAX], tried[PATH_MAX * 4], err[PATH_MAX + 128], avail[64];
	const char *dopt = NULL;
	struct dict d;
	struct rng r;
	unsigned long long seed = 0;
	long v, tries = 0, len = 0;
	int c, st, seeded = 0, rc = 0, noanim = 0;
	char *e;

	while ((c = getopt(argc, argv, "d:l:t:s:nh")) != -1) {
		switch (c) {
		case 'd':
			dopt = optarg;
			break;
		case 'l':
			if (num(optarg, MIN_LEN, MAX_LEN, &len) < 0) {
				fprintf(stderr, "wordle: length must be %d-%d\n", MIN_LEN, MAX_LEN);
				return 2;
			}
			break;
		case 't':
			if (num(optarg, 1, MAX_TRIES, &tries) < 0) {
				fprintf(stderr, "wordle: tries must be 1-%d\n", MAX_TRIES);
				return 2;
			}
			break;
		case 's':
			errno = 0;
			seed = strtoull(optarg, &e, 0);
			if (errno || e == optarg || *e) {
				fprintf(stderr, "wordle: bad seed '%s'\n", optarg);
				return 2;
			}
			seeded = 1;
			break;
		case 'n':
			noanim = 1;
			break;
		case 'h':
			usage(stdout);
			return 0;
		default:
			usage(stderr);
			return 2;
		}
	}
	if (optind < argc) {
		usage(stderr);
		return 2;
	}

	if (dict_find(dopt, path, sizeof path, tried, sizeof tried) < 0) {
		fprintf(stderr, "wordle: cannot read word list; tried:\n%s"
		    "use -d FILE or set WORDLE_DICT\n", tried);
		return 1;
	}
	if (dict_load(&d, path, err, sizeof err) < 0) {
		fprintf(stderr, "wordle: %s\n", err);
		return 1;
	}
	if (dict_total(&d) == 0) {
		fprintf(stderr, "wordle: %s: no usable words (need %d-%d letters, a-z)\n",
		    path, MIN_LEN, MAX_LEN);
		dict_free(&d);
		return 1;
	}
	dict_lengths(&d, avail, sizeof avail);
	if (len && dict_count(&d, (int)len) == 0) {
		fprintf(stderr, "wordle: no %ld-letter words in %s (available: %s)\n",
		    len, path, avail);
		dict_free(&d);
		return 1;
	}
	if (!isatty(0) || !isatty(1)) {
		fprintf(stderr, "wordle: stdin and stdout must be a terminal\n");
		dict_free(&d);
		return 1;
	}
	if (!len) {
		len = 5;
		for (v = MIN_LEN; dict_count(&d, 5) == 0 && v <= MAX_LEN; v++)
			if (dict_count(&d, (int)v)) {
				len = v;
				break;
			}
	}

	rng_seed(&r, seeded ? seed : rng_os_seed());
	input_init();
	if (ui_init() < 0) {
		fprintf(stderr, "wordle: cannot initialise terminal (is $TERM set correctly?)\n");
		dict_free(&d);
		return 1;
	}

	ui_set_anim(!noanim);
	for (st = ST_MENU; st != ST_QUIT; ) {
		int l = (int)len;

		switch (st) {
		case ST_MENU:
			st = menu(&d, &l, (int)tries);
			len = l;
			break;
		case ST_HELP:
			st = help();
			break;
		case ST_PLAY:
			st = play(&d, (int)len, (int)tries, &r);
			break;
		default:
			st = ST_QUIT;
		}
	}
	ui_end();
	dict_free(&d);
	return rc;
}

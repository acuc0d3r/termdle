/*
 * ui.c - curses drawing for wordle: the menu, the help page and the game
 * board, plus two small animations (tile flip on submit, shake on a bad
 * guess).
 *
 * Nothing here reads keys; input.c does that.  Colour is only ever set on
 * tiles and a few highlights, never on the background, so the game keeps
 * whatever theme the terminal already has.
 *
 * Boxes are drawn with ACS characters rather than UTF-8: narrow ncurses
 * cannot place wide characters, and this way it works on any terminfo.
 */

#include <assert.h>
#include <ctype.h>
#include <curses.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "ui.h"

enum { P_GREEN = 1, P_YELLOW, P_GRAY, P_ACCENT, P_MUTED };
enum { L_EMPTY, L_CURSOR, L_TYPED, L_FLIP, L_SCORED };	/* how a cell looks */
enum { NKEYS = 26 };

#define KEYW		4	/* one on-screen key plus the gap after it */
#define KBW		39	/* widest keyboard row: ten keys, nine gaps */
#define CHROME		10	/* rows around the grid, see draw_game() */
#define HDR_MIN		14	/* the header is never narrower than this */
#define HINT_GAP	3
#define LABEL_W		13	/* menu label column; "How to play" is widest */
#define HELP_W		54
#define HELP_H		18	/* rows the help page uses, see ui_help() */
#define REVEAL_MS	700	/* flip budget for one whole row */
#define FLIP_MIN_MS	50
#define FLIP_MAX_MS	110
#define SHAKE_MS	35

static_assert(KBW == 10 * KEYW - 1, "keyboard width");
static_assert(sizeof(((struct game *)0)->key) == NKEYS, "game.key size");

struct style {
	int tw, th;	/* tile size */
	int gx, gy;	/* gap between tiles */
};

/* biggest first; the first one that fits the terminal wins */
static const struct style styles[] = {
	{ 5, 3, 1, 1 }, { 5, 3, 1, 0 }, { 3, 1, 1, 1 }, { 3, 1, 1, 0 }
};
#define NSTYLES		((int)(sizeof styles / sizeof styles[0]))
#define SMALL		(&styles[NSTYLES - 1])

struct layout {
	const struct style *st;
	int gw, gh;		/* grid size in cells */
	int x_grid, y_grid;	/* top left of the grid */
	int y0;			/* top of the whole screen block */
};

struct hint {
	const char *key;
	const char *what;
};

/* animation state; row < 0 means no reveal is running */
struct effect {
	int row;	/* guess row being revealed */
	int done;	/* tiles of that row already shown scored */
	int flip;	/* tile caught mid-flip, or -1 */
	int dx;		/* sideways shove of the row being typed */
};

static const char *const kbrow[3] = { "qwertyuiop", "asdfghjkl", "zxcvbnm" };
static SCREEN *scr;
static bool color;
static bool anim = true;
static struct effect fx = { -1, 0, -1, 0 };

/*
 * fmt - snprintf that reports truncation.
 *
 * buf, sz: destination and its size.  f: printf format.
 * Returns 0 if it all fit, -1 if not; buf is always terminated.
 * Callers skip the element on -1: a clipped label reads as a bug, a
 * missing one as a small terminal.
 */
static int
fmt(char *buf, size_t sz, const char *f, ...)
{
	va_list ap;
	int n;

	va_start(ap, f);
	n = vsnprintf(buf, sz, f, ap);
	va_end(ap);
	if (n < 0 || (size_t)n >= sz)
		return -1;
	return 0;
}

/*
 * set_pair - init_pair() without the short casts at every call site.
 * Returns 0, or -1 if curses refuses the pair.
 */
static int
set_pair(int pair, int fg, int bg)
{
	if (init_pair((short)pair, (short)fg, (short)bg) == ERR)
		return -1;
	return 0;
}

/*
 * init_colors - define the colour pairs.
 *
 * Tiles use fixed 256-colour values with dark text so they read the same
 * on light and dark themes.  Everything else uses the terminal's own
 * background (-1) so a themed or transparent terminal is left alone.
 * Returns 0, or -1 if a pair could not be set; the caller then falls
 * back to monochrome attributes.
 */
static int
init_colors(void)
{
	int dflt;

	dflt = use_default_colors() == OK ? -1 : COLOR_BLACK;
	if (COLORS >= 256) {
		if (set_pair(P_GREEN, 232, 114) < 0 ||
		    set_pair(P_YELLOW, 232, 179) < 0 ||
		    set_pair(P_GRAY, 252, 238) < 0 ||
		    set_pair(P_ACCENT, 114, dflt) < 0 ||
		    set_pair(P_MUTED, 242, dflt) < 0)
			return -1;
		return 0;
	}
	if (set_pair(P_GREEN, COLOR_BLACK, COLOR_GREEN) < 0 ||
	    set_pair(P_YELLOW, COLOR_BLACK, COLOR_YELLOW) < 0 ||
	    set_pair(P_GRAY, COLOR_BLACK, COLOR_WHITE) < 0 ||
	    set_pair(P_ACCENT, COLOR_GREEN, dflt) < 0)
		return -1;
	return 0;
}

/*
 * ui_init - start curses on stdin/stdout.
 * Returns 0, or -1 if the terminal cannot be initialised (no errno;
 * curses does not set one).  Call input_init() first.
 */
int
ui_init(void)
{
	scr = newterm(NULL, stdout, stdin);
	if (scr == NULL)
		return -1;
	if (cbreak() == ERR || noecho() == ERR || keypad(stdscr, TRUE) == ERR)
		goto fail;
	/* not fatal: some terminals cannot hide the cursor or set a delay */
	(void)curs_set(0);
	(void)set_escdelay(25);
	color = false;
	if (has_colors() && start_color() == OK)
		color = init_colors() == 0;
	return 0;
fail:
	ui_end();
	return -1;
}

/*
 * ui_end - give the terminal back.  Safe to call twice.
 */
void
ui_end(void)
{
	if (scr == NULL)
		return;
	(void)curs_set(1);
	endwin();
	delscreen(scr);
	scr = NULL;
}

/*
 * ui_set_anim - switch the flip and shake animations on or off.
 * They are on by default; -n turns them off for slow links.
 */
void
ui_set_anim(bool on)
{
	anim = on;
}

/*
 * scored_attr - attribute for a scored tile or key (S_CORRECT etc).
 * Without colour the three states stay distinguishable by weight alone.
 */
static int
scored_attr(int s)
{
	if (color) {
		if (s == S_CORRECT)
			return (int)(COLOR_PAIR(P_GREEN) | A_BOLD);
		if (s == S_PRESENT)
			return (int)(COLOR_PAIR(P_YELLOW) | A_BOLD);
		return (int)(COLOR_PAIR(P_GRAY) | A_BOLD);
	}
	if (s == S_CORRECT)
		return (int)(A_REVERSE | A_BOLD);
	if (s == S_PRESENT)
		return (int)(A_UNDERLINE | A_BOLD);
	return (int)A_DIM;
}

/*
 * muted_attr - secondary text.  A_DIM is ignored by some terminals, and
 * a ruled-out key that looks like an unused one is a real bug, so with
 * 256 colours use an explicit grey instead.
 */
static int
muted_attr(void)
{
	if (color && COLORS >= 256)
		return (int)COLOR_PAIR(P_MUTED);
	return (int)A_DIM;
}

/*
 * accent_attr - the one highlight colour, used for the menu marker.
 */
static int
accent_attr(void)
{
	if (color)
		return (int)(COLOR_PAIR(P_ACCENT) | A_BOLD);
	return (int)A_BOLD;
}

/*
 * put_str - draw s at (y, x) in attr, clipped at the right edge.
 * curses refuses writes past the last column anyway; clipping here keeps
 * the long help lines from wrapping onto the next row on some terminals.
 */
static void
put_str(int y, int x, int attr, const char *s)
{
	int room;

	room = getmaxx(stdscr) - x;
	if (x < 0 || room <= 0)
		return;
	attron(attr);
	mvaddnstr(y, x, s, room);
	attroff(attr);
}

/*
 * put_center - draw s centred on row y of a screen cols wide.
 */
static void
put_center(int y, int cols, int attr, const char *s)
{
	int x;

	x = (cols - (int)strlen(s)) / 2;
	if (x < 0)
		x = 0;
	put_str(y, x, attr, s);
}

/*
 * put_toast - a short notice as a solid pill, centred on row y.
 * The space of padding each side is what makes it read as a pill and
 * not as reverse-video text.
 */
static void
put_toast(int y, int cols, int attr, const char *s)
{
	char buf[80];

	if (fmt(buf, sizeof buf, " %s ", s) < 0)
		return;
	put_center(y, cols, attr, buf);
}

/*
 * hint_width - columns taken by one hint: key, a space, meaning.
 */
static int
hint_width(const struct hint *h)
{
	return (int)(strlen(h->key) + 1 + strlen(h->what));
}

/*
 * put_hints - key hints on row y: keys bold, meanings muted.
 *
 * h, n: the hints and their count.  Hints that do not fit are dropped
 * from the end, so list the important ones first.  There are no
 * separators; the change of weight does that job.
 */
static void
put_hints(int y, int cols, const struct hint *h, size_t n)
{
	size_t i, fit;
	int w, x, add;

	w = 0;
	fit = 0;
	for (i = 0; i < n; i++) {
		add = hint_width(&h[i]) + (i > 0 ? HINT_GAP : 0);
		if (w + add > cols)
			break;
		w += add;
		fit++;
	}
	x = (cols - w) / 2;
	for (i = 0; i < fit; i++) {
		put_str(y, x, (int)A_BOLD, h[i].key);
		x += (int)strlen(h[i].key) + 1;
		put_str(y, x, muted_attr(), h[i].what);
		x += (int)strlen(h[i].what) + HINT_GAP;
	}
}

/*
 * fill_rect - paint a w by h block of spaces in attr.
 * Spaces carry the background colour, which is all a tile is.
 */
static void
fill_rect(int y, int x, int w, int h, int attr)
{
	int r, c;

	attron(attr);
	for (r = 0; r < h; r++)
		for (c = 0; c < w; c++)
			mvaddch(y + r, x + c, ' ');
	attroff(attr);
}

/*
 * tile_scored - a solid tile with its letter in the middle.
 * st: tile size.  ch: the letter.  s: S_CORRECT, S_PRESENT or S_ABSENT.
 */
static void
tile_scored(int y, int x, const struct style *st, int ch, int s)
{
	int a;

	a = scored_attr(s);
	fill_rect(y, x, st->tw, st->th, a);
	attron(a);
	mvaddch(y + st->th / 2, x + st->tw / 2, (chtype)toupper(ch));
	attroff(a);
}

/*
 * tile_empty - an outlined tile, with ch inside unless it is a space.
 * Small tiles have no room for a box and get brackets instead.
 */
static void
tile_empty(int y, int x, const struct style *st, int ch, int attr)
{
	int i, right, bottom;

	right = x + st->tw - 1;
	bottom = y + st->th - 1;
	attron(attr);
	if (st->th >= 3) {
		mvaddch(y, x, ACS_ULCORNER);
		mvaddch(y, right, ACS_URCORNER);
		mvaddch(bottom, x, ACS_LLCORNER);
		mvaddch(bottom, right, ACS_LRCORNER);
		for (i = x + 1; i < right; i++) {
			mvaddch(y, i, ACS_HLINE);
			mvaddch(bottom, i, ACS_HLINE);
		}
		for (i = y + 1; i < bottom; i++) {
			mvaddch(i, x, ACS_VLINE);
			mvaddch(i, right, ACS_VLINE);
		}
		if (ch != ' ')
			mvaddch(y + st->th / 2, x + st->tw / 2,
			    (chtype)toupper(ch));
	} else {
		mvaddch(y, x, '[');
		mvaddch(y, x + 1, (chtype)toupper(ch));
		mvaddch(y, right, ']');
	}
	attroff(attr);
}

/*
 * tile_flip - a tile caught edge-on: a line where the middle row was.
 * Drawn between the typed look and the scored look it turns into.
 */
static void
tile_flip(int y, int x, const struct style *st)
{
	int i;

	attron((int)A_BOLD);
	for (i = 0; i < st->tw; i++)
		mvaddch(y + st->th / 2, x + i, ACS_HLINE);
	attroff((int)A_BOLD);
}

/*
 * cell_look - how grid cell (r, c) should look right now.
 *
 * Scored rows are solid.  The row being typed shows its letters in bold
 * outlines and the next free cell as a bold empty one, so the eye knows
 * where it is typing; every other cell is a quiet outline.  During a
 * reveal the tiles behind the flip are scored and the rest still look
 * typed.  ncur is the number of letters typed so far.
 */
static int
cell_look(const struct game *g, int ncur, int r, int c)
{
	if (r == fx.row) {
		if (c < fx.done)
			return L_SCORED;
		return c == fx.flip ? L_FLIP : L_TYPED;
	}
	if (r < g->nguess)
		return L_SCORED;
	if (r == g->nguess && g->state == G_PLAY && fx.row < 0) {
		if (c < ncur)
			return L_TYPED;
		return c == ncur ? L_CURSOR : L_EMPTY;
	}
	return L_EMPTY;
}

/*
 * cell_char - the letter belonging in cell (r, c), or a space.
 * Submitted rows come from the game, the row being typed from cur.
 */
static int
cell_char(const struct game *g, const char *cur, int ncur, int r, int c)
{
	if (r < g->nguess)
		return g->guess[r][c];
	if (r == g->nguess && c < ncur)
		return cur[c];
	return ' ';
}

/*
 * draw_grid - every cell of the board.
 * The row being typed is shifted by fx.dx, which is how the shake works.
 */
static void
draw_grid(const struct layout *lo, const struct game *g, const char *cur,
    int ncur)
{
	const struct style *st;
	int r, c, y, x, ch;

	st = lo->st;
	for (r = 0; r < g->tries; r++) {
		y = lo->y_grid + r * (st->th + st->gy);
		for (c = 0; c < g->len; c++) {
			x = lo->x_grid + c * (st->tw + st->gx);
			if (r == g->nguess)
				x += fx.dx;
			ch = cell_char(g, cur, ncur, r, c);
			switch (cell_look(g, ncur, r, c)) {
			case L_SCORED:
				tile_scored(y, x, st, ch, g->score[r][c]);
				break;
			case L_FLIP:
				tile_flip(y, x, st);
				break;
			case L_TYPED:
			case L_CURSOR:
				tile_empty(y, x, st, ch, (int)A_BOLD);
				break;
			default:
				tile_empty(y, x, st, ' ', muted_attr());
				break;
			}
		}
	}
}

/*
 * keys_shown - the keyboard state to draw, key[NKEYS] best score a-z.
 *
 * Normally that is just g->key.  Mid-reveal the new row's letters have
 * already been folded in, but the keyboard should only change once the
 * flip is over, so rebuild it from the rows before the revealed one.
 */
static void
keys_shown(const struct game *g, unsigned char *key, size_t ksz)
{
	int r, c, k;

	if (fx.row < 0) {
		memcpy(key, g->key, ksz);
		return;
	}
	memset(key, 0, ksz);
	for (r = 0; r < fx.row; r++) {
		for (c = 0; c < g->len; c++) {
			k = g->guess[r][c] - 'a';
			if (k < 0 || (size_t)k >= ksz)
				continue;
			if (g->score[r][c] > key[k])
				key[k] = g->score[r][c];
		}
	}
}

/*
 * draw_keyboard - three staggered rows at y.  Letters not tried yet are
 * bold, ruled-out ones fade, the rest take their tile colour.
 */
static void
draw_keyboard(int y, int cols, const unsigned char *key)
{
	int r, i, n, x, a, s;

	for (r = 0; r < 3; r++) {
		n = (int)strlen(kbrow[r]);
		x = (cols - (KEYW * n - 1)) / 2;
		for (i = 0; i < n; i++, x += KEYW) {
			s = key[kbrow[r][i] - 'a'];
			if (s == S_ABSENT)
				a = muted_attr();
			else if (s == S_NONE)
				a = (int)A_BOLD;
			else
				a = scored_attr(s);
			attron(a);
			mvaddch(y + r, x, ' ');
			mvaddch(y + r, x + 1, (chtype)toupper(kbrow[r][i]));
			mvaddch(y + r, x + 2, ' ');
			attroff(a);
		}
	}
}

/*
 * layout_fit - pick the largest tile style that fits the terminal.
 *
 * Returns 0 with lo filled in, or -1 if even the smallest style is too
 * big; lo then still holds the smallest style's grid size so the caller
 * can say what would fit.  Everything is bounded by MAX_LEN and
 * MAX_TRIES, so none of this arithmetic can overflow.
 */
static int
layout_fit(struct layout *lo, const struct game *g, int rows, int cols)
{
	int i, w;

	for (i = 0; i < NSTYLES; i++) {
		lo->st = &styles[i];
		lo->gw = g->len * styles[i].tw + (g->len - 1) * styles[i].gx;
		lo->gh = g->tries * styles[i].th +
		    (g->tries - 1) * styles[i].gy;
		w = lo->gw > KBW ? lo->gw : KBW;
		if (lo->gh + CHROME > rows || w > cols)
			continue;
		lo->y0 = (rows - (lo->gh + CHROME)) / 2;
		lo->y_grid = lo->y0 + 2;
		lo->x_grid = (cols - lo->gw) / 2;
		return 0;
	}
	return -1;
}

/*
 * draw_too_small - say what size the board needs instead of drawing it.
 */
static void
draw_too_small(const struct layout *lo, int rows, int cols)
{
	char buf[64];
	int w;

	w = lo->gw > KBW ? lo->gw : KBW;
	put_center(rows / 2 - 1, cols, (int)A_BOLD, "Terminal too small");
	if (fmt(buf, sizeof buf, "needs %dx%d, have %dx%d", w,
	    lo->gh + CHROME, cols, rows) == 0)
		put_center(rows / 2, cols, (int)A_NORMAL, buf);
	put_center(rows / 2 + 1, cols, muted_attr(), "resize, or esc for menu");
}

/*
 * draw_header - title on the left, guess counter on the right, both
 * lined up with the edges of the grid.  Mid-reveal the counter already
 * shows the row being turned over.
 */
static void
draw_header(const struct layout *lo, const struct game *g, int cols)
{
	char buf[16];
	int hw, x, shown;

	hw = lo->gw > HDR_MIN ? lo->gw : HDR_MIN;
	x = (cols - hw) / 2;
	put_str(lo->y0, x, (int)A_BOLD, "wordle");
	if (fx.row >= 0)
		shown = fx.row + 1;
	else
		shown = g->state == G_PLAY ? g->nguess + 1 : g->nguess;
	if (fmt(buf, sizeof buf, "%d/%d", shown, g->tries) < 0)
		return;
	put_str(lo->y0, x + hw - (int)strlen(buf), muted_attr(), buf);
}

/*
 * draw_status - the one-line notice under the grid: the result when the
 * game is over, otherwise msg ("Not in word list" and the like).
 * Nothing is shown during a reveal so the result is not given away
 * before the last tile turns.
 */
static void
draw_status(const struct layout *lo, const struct game *g, int cols,
    const char *msg)
{
	char buf[64], word[MAX_LEN + 1];
	int y, notice;
	size_t i;

	if (fx.row >= 0)
		return;
	y = lo->y_grid + lo->gh + 1;
	notice = (int)(A_REVERSE | A_BOLD);
	if (g->state == G_WON) {
		if (fmt(buf, sizeof buf, "Solved in %d %s", g->nguess,
		    g->nguess == 1 ? "guess" : "guesses") == 0)
			put_toast(y, cols, scored_attr(S_CORRECT), buf);
	} else if (g->state == G_LOST) {
		for (i = 0; i < MAX_LEN && g->answer[i] != '\0'; i++)
			word[i] = (char)toupper((unsigned char)g->answer[i]);
		word[i] = '\0';
		if (fmt(buf, sizeof buf, "The word was %s", word) == 0)
			put_toast(y, cols, notice, buf);
	} else if (msg != NULL && *msg != '\0') {
		put_toast(y, cols, notice, msg);
	}
}

/*
 * draw_footer - key hints for the current state of the game.
 */
static void
draw_footer(int y, int cols, const struct game *g)
{
	static const struct hint play[] = {
		{ "enter", "submit" }, { "backspace", "delete" },
		{ "esc", "menu" }
	};
	static const struct hint over[] = {
		{ "enter", "play again" }, { "esc", "menu" }, { "q", "quit" }
	};

	if (g->state == G_PLAY || fx.row >= 0)
		put_hints(y, cols, play, sizeof play / sizeof play[0]);
	else
		put_hints(y, cols, over, sizeof over / sizeof over[0]);
}

/*
 * draw_game - the whole board into the screen buffer, no refresh.
 *
 *	header
 *	(blank)
 *	grid
 *	(blank)
 *	notice
 *	(blank)
 *	keyboard, three rows
 *	(blank)
 *	hints
 *
 * That is gh + CHROME rows, centred vertically.  cur and ncur are the
 * letters typed so far; msg is a notice or NULL.
 */
static void
draw_game(const struct game *g, const char *cur, int ncur, const char *msg)
{
	struct layout lo;
	unsigned char key[NKEYS];
	int rows, cols;

	rows = getmaxy(stdscr);
	cols = getmaxx(stdscr);
	erase();
	if (layout_fit(&lo, g, rows, cols) < 0) {
		draw_too_small(&lo, rows, cols);
		return;
	}
	draw_header(&lo, g, cols);
	draw_grid(&lo, g, cur, ncur);
	draw_status(&lo, g, cols, msg);
	keys_shown(g, key, sizeof key);
	draw_keyboard(lo.y_grid + lo.gh + 3, cols, key);
	draw_footer(lo.y_grid + lo.gh + 7, cols, g);
}

/*
 * frame - draw the game, show it, and hold it for ms milliseconds.
 * Returns 0, or -1 if the terminal write failed.
 */
static int
frame(const struct game *g, const char *cur, int ncur, const char *msg,
    int ms)
{
	draw_game(g, cur, ncur, msg);
	if (refresh() == ERR)
		return -1;
	napms(ms);
	return 0;
}

/*
 * ui_game - draw the game screen.
 *
 * g: the game.  cur, ncur: the guess being typed and its length.
 * msg: a notice for the line under the grid, or NULL.
 * Returns 0, or -1 if the terminal write failed.
 */
int
ui_game(const struct game *g, const char *cur, int ncur, const char *msg)
{
	draw_game(g, cur, ncur, msg);
	return refresh() == ERR ? -1 : 0;
}

/*
 * ui_reveal - turn the tiles of guess row over one at a time.
 *
 * Call right after game_submit(); the scores are already in g.  Each tile
 * collapses to a line and opens again scored.  The whole row takes about
 * REVEAL_MS however long the word is, and type-ahead is thrown away at the
 * end so keys hit during the show do not leak into the next guess.
 * XXX the show cannot be skipped with a key; -n is the only way out.
 * Returns 0, or -1 if the terminal write failed.
 */
int
ui_reveal(const struct game *g, int row)
{
	int i, ms, rc;

	if (!anim || row < 0 || row >= g->nguess)
		return 0;
	ms = REVEAL_MS / g->len;
	if (ms < FLIP_MIN_MS)
		ms = FLIP_MIN_MS;
	if (ms > FLIP_MAX_MS)
		ms = FLIP_MAX_MS;
	rc = 0;
	fx.row = row;
	for (i = 0; i < g->len && rc == 0; i++) {
		fx.done = i;
		fx.flip = i;
		rc = frame(g, "", 0, NULL, ms / 2);
		fx.done = i + 1;
		fx.flip = -1;
		if (rc == 0)
			rc = frame(g, "", 0, NULL, ms - ms / 2);
	}
	fx.row = -1;
	fx.done = 0;
	fx.flip = -1;
	(void)flushinp();
	return rc;
}

/*
 * ui_shake - rattle the row being typed, for a guess that was refused.
 * The notice in msg is on screen for the whole shake.
 * Returns 0, or -1 if the terminal write failed.
 */
int
ui_shake(const struct game *g, const char *cur, int ncur, const char *msg)
{
	static const int dx[] = { -2, 2, -2, 2, -1, 1, 0 };
	size_t i;
	int rc;

	if (!anim)
		return 0;
	rc = 0;
	for (i = 0; i < sizeof dx / sizeof dx[0] && rc == 0; i++) {
		fx.dx = dx[i];
		rc = frame(g, cur, ncur, msg, SHAKE_MS);
	}
	fx.dx = 0;
	(void)flushinp();
	return rc;
}

/*
 * draw_logo - WORDLE spelled in tiles, with a made-up scoring so all
 * three colours show up before the first game.
 */
static void
draw_logo(int y, int cols, const struct style *st)
{
	static const char word[] = "WORDLE";
	static const int look[] = {
		S_CORRECT, S_PRESENT, S_ABSENT, S_CORRECT, S_ABSENT, S_PRESENT
	};
	int i, n, x;

	n = (int)sizeof word - 1;
	x = (cols - (n * st->tw + (n - 1) * st->gx)) / 2;
	if (x < 0)
		x = 0;
	for (i = 0; i < n; i++)
		tile_scored(y, x + i * (st->tw + st->gx), st, word[i], look[i]);
}

/*
 * draw_item - one menu row: a marker and bold label when selected.
 */
static void
draw_item(int y, int x, bool selected, const char *label)
{
	if (selected) {
		put_str(y, x, accent_attr(), ">");
		put_str(y, x + 2, (int)A_BOLD, label);
	} else {
		put_str(y, x + 2, (int)A_NORMAL, label);
	}
}

/*
 * strip_width - columns taken by the row of selectable lengths.
 * pad: cells get a space each side (the selected one is a tile), or not,
 * for terminals too narrow for that.
 */
static int
strip_width(bool pad)
{
	int len, w;

	w = 0;
	for (len = MIN_LEN; len <= MAX_LEN; len++)
		w += (len >= 10 ? 2 : 1) + (pad ? 2 : 1);
	return pad ? w : w - 1;
}

/*
 * draw_strip - every word length as a row of numbers.  The chosen one is
 * a tile; lengths the dictionary has no words for fade out.  That one
 * row replaces both a "< 5 >" picker and a list of available lengths.
 */
static void
draw_strip(int y, int x, int len, const struct dict *d, bool pad)
{
	char buf[8];
	int l, a;
	bool have;

	for (l = MIN_LEN; l <= MAX_LEN; l++) {
		have = dict_count(d, l) > 0;
		if (l == len)
			a = scored_attr(have ? S_CORRECT : S_ABSENT);
		else
			a = have ? (int)A_NORMAL : muted_attr();
		if (fmt(buf, sizeof buf, pad ? " %d " : "%d", l) < 0)
			return;
		put_str(y, x, a, buf);
		x += (int)strlen(buf) + (pad ? 0 : 1);
	}
}

/*
 * draw_length_info - how many words and guesses the chosen length gives.
 * tries is the -t override, 0 for the normal length + 1 (at least 6).
 */
static void
draw_length_info(int y, int x, int len, int tries, const struct dict *d)
{
	char buf[64];
	size_t n;
	int t, rc;

	n = dict_count(d, len);
	t = tries > 0 ? tries : game_tries(len);
	if (n == 0)
		rc = fmt(buf, sizeof buf, "no words of this length");
	else
		rc = fmt(buf, sizeof buf, "%zu %s, %d %s", n,
		    n == 1 ? "word" : "words", t, t == 1 ? "guess" : "guesses");
	if (rc == 0)
		put_str(y, x, muted_attr(), buf);
}

/*
 * ui_menu - draw the main menu.
 *
 * sel: highlighted row (M_START ...).  len: chosen word length.
 * tries: the -t override, or 0.  d: the dictionary, for which lengths
 * exist.  msg: a notice shown under the menu, or NULL.
 * Returns 0, or -1 if the terminal write failed.
 */
int
ui_menu(int sel, int len, int tries, const struct dict *d, const char *msg)
{
	static const char *const label[M_N] = {
		[M_START] = "Play", [M_LEN] = "Length",
		[M_HELP] = "How to play", [M_QUIT] = "Quit"
	};
	static const int off[M_N] = {
		[M_START] = 0, [M_LEN] = 1, [M_HELP] = 3, [M_QUIT] = 4
	};
	static const struct hint hints[] = {
		{ "enter", "select" }, { "up/down", "move" },
		{ "left/right", "length" }, { "q", "quit" }
	};
	const struct style *logo;
	int rows, cols, lh, y0, y, x0, xs, i;
	bool pad;

	rows = getmaxy(stdscr);
	cols = getmaxx(stdscr);
	logo = rows >= 20 ? &styles[0] : SMALL;
	lh = logo->th;
	pad = 2 + LABEL_W + strip_width(true) <= cols;
	y0 = (rows - (lh + 10)) / 2;
	if (y0 < 0)
		y0 = 0;
	x0 = (cols - (2 + LABEL_W + strip_width(pad))) / 2;
	if (x0 < 0)
		x0 = 0;
	xs = x0 + 2 + LABEL_W;
	y = y0 + lh + 1;

	erase();
	draw_logo(y0, cols, logo);
	for (i = 0; i < M_N; i++)
		draw_item(y + off[i], x0, i == sel, label[i]);
	draw_strip(y + off[M_LEN], xs, len, d, pad);
	draw_length_info(y + off[M_LEN] + 1, xs, len, tries, d);
	if (msg != NULL && *msg != '\0')
		put_toast(y + 6, cols, (int)(A_REVERSE | A_BOLD), msg);
	put_hints(y + 8, cols, hints, sizeof hints / sizeof hints[0]);
	return refresh() == ERR ? -1 : 0;
}

/*
 * ui_help - draw the how-to-play page.
 *
 * The example row is scored by score_guess() itself, so the colours
 * shown here can never disagree with the rules.  Returns 0, or -1 if the
 * terminal write failed.
 */
int
ui_help(void)
{
	static const char guess[] = "crane";
	static const char *const intro[] = {
		"Guess the hidden word. Each guess must be a real word",
		"of the length you chose. You get length + 1 guesses,",
		"never fewer than 6."
	};
	static const char *const outro[] = {
		"A letter is only marked as often as it appears in",
		"the word, and exact matches are marked first."
	};
	static const struct {
		int pos;		/* which letter of guess it explains */
		const char *text;
	} legend[] = {
		{ 1, "right letter, right place" },
		{ 0, "right letter, wrong place" },
		{ 3, "not in the word" }
	};
	static const struct hint back[] = { { "any key", "back" } };
	unsigned char sc[sizeof guess - 1];
	size_t i;
	int rows, cols, y, x0;

	score_guess("trace", guess, (int)sizeof sc, sc);
	rows = getmaxy(stdscr);
	cols = getmaxx(stdscr);
	y = (rows - HELP_H) / 2;
	if (y < 0)
		y = 0;
	x0 = (cols - HELP_W) / 2;
	if (x0 < 0)
		x0 = 0;

	erase();
	put_str(y, x0, (int)A_BOLD, "How to play");
	y += 2;
	for (i = 0; i < sizeof intro / sizeof intro[0]; i++)
		put_str(y++, x0, (int)A_NORMAL, intro[i]);
	y++;
	put_str(y, x0, (int)A_NORMAL,
	    "Say the answer is TRACE and you guess CRANE:");
	y += 2;
	for (i = 0; i < sizeof sc; i++)
		tile_scored(y, x0 + 2 + (int)i * 4, SMALL, guess[i], sc[i]);
	y += 2;
	for (i = 0; i < sizeof legend / sizeof legend[0]; i++, y++) {
		tile_scored(y, x0 + 2, SMALL, guess[legend[i].pos],
		    sc[legend[i].pos]);
		put_str(y, x0 + 6, (int)A_NORMAL, legend[i].text);
	}
	y++;
	for (i = 0; i < sizeof outro / sizeof outro[0]; i++)
		put_str(y++, x0, (int)A_NORMAL, outro[i]);
	y++;
	put_hints(y, cols, back, sizeof back / sizeof back[0]);
	return refresh() == ERR ? -1 : 0;
}

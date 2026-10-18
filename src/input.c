#include <ctype.h>
#include <curses.h>
#include <errno.h>
#include <signal.h>
#include <string.h>
#include "input.h"

static volatile sig_atomic_t quit;

static void onsig(int sig)
{
	(void)sig;
	quit = 1;
}

void input_init(void)
{
	struct sigaction sa;

	memset(&sa, 0, sizeof sa);
	sa.sa_handler = onsig;		/* no SA_RESTART: getch() must return */
	sigemptyset(&sa.sa_mask);
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGHUP, &sa, NULL);
}

struct event input_get(void)
{
	struct event e = { EV_NONE, 0 };
	int c;

	errno = 0;
	c = getch();
	if (quit) {
		e.type = EV_QUIT;
		return e;
	}
	switch (c) {
	case ERR:
		if (errno != EINTR)
			e.type = EV_QUIT;	/* EOF or dead terminal */
		return e;
	case KEY_RESIZE:	e.type = EV_RESIZE; return e;
	case KEY_UP:		e.type = EV_UP; return e;
	case KEY_DOWN:		e.type = EV_DOWN; return e;
	case KEY_LEFT:		e.type = EV_LEFT; return e;
	case KEY_RIGHT:		e.type = EV_RIGHT; return e;
	case KEY_BACKSPACE: case 127: case 8:
				e.type = EV_BACK; return e;
	case KEY_ENTER: case '\n': case '\r':
				e.type = EV_ENTER; return e;
	case 27:		e.type = EV_ESC; return e;
	}
	if (c > 0 && c < 127 && isprint(c)) {
		e.type = EV_CHAR;
		e.ch = tolower(c);
	}
	return e;
}

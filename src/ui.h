/* ui.h - curses screens for wordle: menu, help page and game board */

#ifndef UI_H
#define UI_H

#include <stdbool.h>
#include "dict.h"
#include "game.h"

/* menu rows, in the order they are drawn */
enum { M_START, M_LEN, M_HELP, M_QUIT, M_N };

/*
 * All of these return 0 on success and -1 if the terminal write failed,
 * except ui_end() and ui_set_anim().  A dead terminal also shows up in
 * input_get() as EV_QUIT, so callers may simply leave on -1.
 */
int	ui_init(void);
void	ui_end(void);
void	ui_set_anim(bool on);
int	ui_menu(int sel, int len, int tries, const struct dict *d,
	    const char *msg);
int	ui_help(void);
int	ui_game(const struct game *g, const char *cur, int ncur,
	    const char *msg);
int	ui_reveal(const struct game *g, int row);
int	ui_shake(const struct game *g, const char *cur, int ncur,
	    const char *msg);

#endif

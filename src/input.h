#ifndef INPUT_H
#define INPUT_H

enum {
	EV_NONE, EV_CHAR, EV_BACK, EV_ENTER, EV_ESC,
	EV_UP, EV_DOWN, EV_LEFT, EV_RIGHT, EV_RESIZE, EV_QUIT
};

struct event { int type, ch; };

void input_init(void);		/* call before ui_init() */
struct event input_get(void);

#endif

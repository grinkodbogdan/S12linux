/* Terminal emulator: a shell on a pseudo-terminal, with a VT100/ANSI screen. */
#ifndef TERM_H
#define TERM_H

#include <sys/types.h>

#define TERM_MAX_COLS 200
#define TERM_MAX_ROWS 80
#define TERM_SCROLLBACK 200

typedef struct {
	char ch;
	unsigned char fg, bg;   /* palette indices, 16 = default */
	unsigned char attr;     /* ATTR_* */
} Cell;

enum { ATTR_BOLD = 1, ATTR_REVERSE = 2, ATTR_UNDERLINE = 4 };

typedef struct {
	int fd;          /* master side of the pseudo-terminal */
	pid_t pid;
	int alive;
	int cols, rows;
	Cell screen[TERM_MAX_ROWS][TERM_MAX_COLS];
	Cell scrollback[TERM_SCROLLBACK][TERM_MAX_COLS];
	int sb_count, sb_start;  /* scrollback ring */
	int scroll;              /* lines scrolled back by the user */
	int cx, cy;              /* cursor */
	int saved_cx, saved_cy;
	int top, bottom;         /* scrolling region, bottom exclusive */
	int cursor_visible;
	int wrap_pending;
	unsigned char fg, bg, attr;
	/* Escape sequence parser */
	int state;
	int params[16], nparams;
	int private_mode;
} Term;

/* Starts `argv` on a new pseudo-terminal. Returns 0 on success */
int term_spawn(Term *t, int cols, int rows, char *const argv[]);
/* Reads pending output from the program. Returns 1 if the screen changed */
int term_poll(Term *t);
void term_write(Term *t, const char *s, int len);
void term_resize(Term *t, int cols, int rows);
void term_close(Term *t);
/* Returns the cell at row `y` of the view, which takes the scrollback offset into account */
const Cell *term_view_row(Term *t, int y);

#endif

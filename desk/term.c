/* Terminal emulator. See term.h. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#include "term.h"

#define DEF 16

static void clear_cells(Term *t, Cell *c, int n) {
	for (int i = 0; i < n; i++) {
		c[i].ch = ' ';
		c[i].fg = DEF;
		c[i].bg = t->bg;
		c[i].attr = 0;
	}
}

static void reset(Term *t) {
	t->fg = DEF;
	t->bg = DEF;
	t->attr = 0;
	t->cx = t->cy = 0;
	t->top = 0;
	t->bottom = t->rows;
	t->cursor_visible = 1;
	t->wrap_pending = 0;
	t->state = 0;
	for (int y = 0; y < TERM_MAX_ROWS; y++)
		clear_cells(t, t->screen[y], TERM_MAX_COLS);
}

int term_spawn(Term *t, int cols, int rows, char *const argv[]) {
	memset(t, 0, sizeof(*t));
	t->cols = cols < TERM_MAX_COLS ? cols : TERM_MAX_COLS;
	t->rows = rows < TERM_MAX_ROWS ? rows : TERM_MAX_ROWS;
	reset(t);
	int fd = posix_openpt(O_RDWR | O_NOCTTY);
	if (fd < 0)
		return -1;
	if (grantpt(fd) < 0 || unlockpt(fd) < 0) {
		close(fd);
		return -1;
	}
	char *name = ptsname(fd);
	if (!name) {
		close(fd);
		return -1;
	}
	struct winsize ws = {.ws_row = t->rows, .ws_col = t->cols};
	ioctl(fd, TIOCSWINSZ, &ws);
	pid_t pid = fork();
	if (pid < 0) {
		close(fd);
		return -1;
	}
	if (pid == 0) {
		setsid();
		int s = open(name, O_RDWR);
		if (s < 0)
			_exit(127);
		ioctl(s, TIOCSCTTY, 0);
		dup2(s, 0);
		dup2(s, 1);
		dup2(s, 2);
		if (s > 2)
			close(s);
		for (int i = 3; i < 64; i++)
			close(i);
		signal(SIGPIPE, SIG_DFL);
		setenv("TERM", "linux", 1);
		execv(argv[0], argv);
		_exit(127);
	}
	fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
	t->fd = fd;
	t->pid = pid;
	t->alive = 1;
	return 0;
}

static void push_scrollback(Term *t, Cell *row) {
	int idx = (t->sb_start + t->sb_count) % TERM_SCROLLBACK;
	if (t->sb_count == TERM_SCROLLBACK) {
		t->sb_start = (t->sb_start + 1) % TERM_SCROLLBACK;
	} else {
		t->sb_count++;
	}
	memcpy(t->scrollback[idx], row, sizeof(t->scrollback[idx]));
}

/* Scrolls the region [top, bottom) up by `n` lines */
static void scroll_up(Term *t, int top, int bottom, int n) {
	for (int i = 0; i < n; i++) {
		if (top == 0 && bottom == t->rows)
			push_scrollback(t, t->screen[0]);
		memmove(&t->screen[top], &t->screen[top + 1], sizeof(t->screen[0]) * (bottom - top - 1));
		clear_cells(t, t->screen[bottom - 1], TERM_MAX_COLS);
	}
}

static void scroll_down(Term *t, int top, int bottom, int n) {
	for (int i = 0; i < n; i++) {
		memmove(&t->screen[top + 1], &t->screen[top], sizeof(t->screen[0]) * (bottom - top - 1));
		clear_cells(t, t->screen[top], TERM_MAX_COLS);
	}
}

static void newline(Term *t) {
	if (t->cy == t->bottom - 1)
		scroll_up(t, t->top, t->bottom, 1);
	else if (t->cy < t->rows - 1)
		t->cy++;
}

static void put(Term *t, char ch) {
	if (t->wrap_pending) {
		t->cx = 0;
		newline(t);
		t->wrap_pending = 0;
	}
	Cell *c = &t->screen[t->cy][t->cx];
	c->ch = ch;
	c->fg = t->fg;
	c->bg = t->bg;
	c->attr = t->attr;
	if (t->cx == t->cols - 1)
		t->wrap_pending = 1;
	else
		t->cx++;
}

static int param(Term *t, int i, int def) {
	return i < t->nparams && t->params[i] > 0 ? t->params[i] : def;
}

static void clamp_cursor(Term *t) {
	if (t->cx < 0) t->cx = 0;
	if (t->cx >= t->cols) t->cx = t->cols - 1;
	if (t->cy < 0) t->cy = 0;
	if (t->cy >= t->rows) t->cy = t->rows - 1;
}

static void sgr(Term *t) {
	if (t->nparams == 0) {
		t->fg = t->bg = DEF;
		t->attr = 0;
		return;
	}
	for (int i = 0; i < t->nparams; i++) {
		int p = t->params[i];
		if (p == 0) {
			t->fg = t->bg = DEF;
			t->attr = 0;
		} else if (p == 1) {
			t->attr |= ATTR_BOLD;
		} else if (p == 4) {
			t->attr |= ATTR_UNDERLINE;
		} else if (p == 7) {
			t->attr |= ATTR_REVERSE;
		} else if (p == 22) {
			t->attr &= ~ATTR_BOLD;
		} else if (p == 24) {
			t->attr &= ~ATTR_UNDERLINE;
		} else if (p == 27) {
			t->attr &= ~ATTR_REVERSE;
		} else if (p >= 30 && p <= 37) {
			t->fg = p - 30;
		} else if (p == 39) {
			t->fg = DEF;
		} else if (p >= 40 && p <= 47) {
			t->bg = p - 40;
		} else if (p == 49) {
			t->bg = DEF;
		} else if (p >= 90 && p <= 97) {
			t->fg = p - 90 + 8;
		} else if (p >= 100 && p <= 107) {
			t->bg = p - 100 + 8;
		} else if ((p == 38 || p == 48) && i + 2 < t->nparams && t->params[i + 1] == 5) {
			/* 256 colors: keep the 16 first, approximate the others with white */
			int c = t->params[i + 2];
			c = c < 16 ? c : 7;
			if (p == 38)
				t->fg = c;
			else
				t->bg = c;
			i += 2;
		}
	}
}

static void csi(Term *t, char final) {
	int n = param(t, 0, 1);
	switch (final) {
	case 'A': t->cy -= n; break;
	case 'B': case 'e': t->cy += n; break;
	case 'C': case 'a': t->cx += n; break;
	case 'D': t->cx -= n; break;
	case 'E': t->cx = 0; t->cy += n; break;
	case 'F': t->cx = 0; t->cy -= n; break;
	case 'G': case '`': t->cx = n - 1; break;
	case 'd': t->cy = n - 1; break;
	case 'H': case 'f':
		t->cy = param(t, 0, 1) - 1;
		t->cx = param(t, 1, 1) - 1;
		break;
	case 'J': {
		int mode = t->nparams ? t->params[0] : 0;
		if (mode == 0) {
			clear_cells(t, &t->screen[t->cy][t->cx], TERM_MAX_COLS - t->cx);
			for (int y = t->cy + 1; y < t->rows; y++)
				clear_cells(t, t->screen[y], TERM_MAX_COLS);
		} else if (mode == 1) {
			for (int y = 0; y < t->cy; y++)
				clear_cells(t, t->screen[y], TERM_MAX_COLS);
			clear_cells(t, t->screen[t->cy], t->cx + 1);
		} else {
			for (int y = 0; y < t->rows; y++)
				clear_cells(t, t->screen[y], TERM_MAX_COLS);
		}
		break;
	}
	case 'K': {
		int mode = t->nparams ? t->params[0] : 0;
		if (mode == 0)
			clear_cells(t, &t->screen[t->cy][t->cx], TERM_MAX_COLS - t->cx);
		else if (mode == 1)
			clear_cells(t, t->screen[t->cy], t->cx + 1);
		else
			clear_cells(t, t->screen[t->cy], TERM_MAX_COLS);
		break;
	}
	case 'L':
		if (t->cy >= t->top && t->cy < t->bottom)
			scroll_down(t, t->cy, t->bottom, n);
		break;
	case 'M':
		if (t->cy >= t->top && t->cy < t->bottom)
			for (int i = 0; i < n; i++) {
				memmove(&t->screen[t->cy], &t->screen[t->cy + 1],
					sizeof(t->screen[0]) * (t->bottom - t->cy - 1));
				clear_cells(t, t->screen[t->bottom - 1], TERM_MAX_COLS);
			}
		break;
	case 'P': {
		Cell *row = t->screen[t->cy];
		if (n > t->cols - t->cx) n = t->cols - t->cx;
		memmove(&row[t->cx], &row[t->cx + n], sizeof(Cell) * (t->cols - t->cx - n));
		clear_cells(t, &row[t->cols - n], n);
		break;
	}
	case '@': {
		Cell *row = t->screen[t->cy];
		if (n > t->cols - t->cx) n = t->cols - t->cx;
		memmove(&row[t->cx + n], &row[t->cx], sizeof(Cell) * (t->cols - t->cx - n));
		clear_cells(t, &row[t->cx], n);
		break;
	}
	case 'X':
		if (n > t->cols - t->cx) n = t->cols - t->cx;
		clear_cells(t, &t->screen[t->cy][t->cx], n);
		break;
	case 'S': scroll_up(t, t->top, t->bottom, n); break;
	case 'T': scroll_down(t, t->top, t->bottom, n); break;
	case 'm': sgr(t); break;
	case 'r':
		t->top = param(t, 0, 1) - 1;
		t->bottom = param(t, 1, t->rows);
		if (t->top < 0 || t->bottom > t->rows || t->top >= t->bottom) {
			t->top = 0;
			t->bottom = t->rows;
		}
		t->cx = t->cy = 0;
		break;
	case 's': t->saved_cx = t->cx; t->saved_cy = t->cy; break;
	case 'u': t->cx = t->saved_cx; t->cy = t->saved_cy; break;
	case 'n':
		if (param(t, 0, 0) == 6) {
			char buf[32];
			int len = snprintf(buf, sizeof(buf), "\033[%d;%dR", t->cy + 1, t->cx + 1);
			term_write(t, buf, len);
		}
		break;
	case 'h': case 'l':
		if (t->private_mode && param(t, 0, 0) == 25)
			t->cursor_visible = final == 'h';
		break;
	}
	t->wrap_pending = 0;
	clamp_cursor(t);
}

static void feed(Term *t, unsigned char ch) {
	switch (t->state) {
	case 0:
		if (ch == 27) {
			t->state = 1;
		} else if (ch == '\r') {
			t->cx = 0;
			t->wrap_pending = 0;
		} else if (ch == '\n' || ch == 11 || ch == 12) {
			newline(t);
			t->wrap_pending = 0;
		} else if (ch == '\b') {
			if (t->cx > 0)
				t->cx--;
			t->wrap_pending = 0;
		} else if (ch == '\t') {
			int nx = (t->cx / 8 + 1) * 8;
			t->cx = nx < t->cols ? nx : t->cols - 1;
		} else if (ch == 7 || ch < 32) {
			/* Bell and other controls: ignored */
		} else if (ch >= 0x80) {
			/* UTF-8: show one placeholder per character */
			if ((ch & 0xc0) != 0x80)
				put(t, '?');
		} else {
			put(t, ch);
		}
		break;
	case 1: /* after ESC */
		t->state = 0;
		if (ch == '[') {
			t->state = 2;
			t->nparams = 0;
			t->params[0] = 0;
			t->private_mode = 0;
		} else if (ch == ']') {
			t->state = 3;
		} else if (ch == '(' || ch == ')') {
			t->state = 4;
		} else if (ch == '7') {
			t->saved_cx = t->cx;
			t->saved_cy = t->cy;
		} else if (ch == '8') {
			t->cx = t->saved_cx;
			t->cy = t->saved_cy;
		} else if (ch == 'D') {
			newline(t);
		} else if (ch == 'M') {
			if (t->cy == t->top)
				scroll_down(t, t->top, t->bottom, 1);
			else if (t->cy > 0)
				t->cy--;
		} else if (ch == 'E') {
			t->cx = 0;
			newline(t);
		} else if (ch == 'c') {
			reset(t);
		}
		break;
	case 2: /* CSI */
		if (ch == '?') {
			t->private_mode = 1;
		} else if (ch >= '0' && ch <= '9') {
			if (t->nparams == 0)
				t->nparams = 1;
			int *p = &t->params[t->nparams - 1];
			*p = *p * 10 + (ch - '0');
		} else if (ch == ';') {
			if (t->nparams == 0)
				t->nparams = 1;
			if (t->nparams < 16)
				t->params[t->nparams++] = 0;
		} else if (ch >= 0x40 && ch <= 0x7e) {
			csi(t, ch);
			t->state = 0;
		}
		break;
	case 3: /* OSC: skipped until BEL or ESC \ */
		if (ch == 7)
			t->state = 0;
		else if (ch == 27)
			t->state = 1;
		break;
	case 4: /* character set selection: ignored */
		t->state = 0;
		break;
	}
}

int term_poll(Term *t) {
	if (!t->alive)
		return 0;
	char buf[4096];
	int changed = 0;
	for (;;) {
		ssize_t n = read(t->fd, buf, sizeof(buf));
		if (n > 0) {
			for (ssize_t i = 0; i < n; i++)
				feed(t, buf[i]);
			changed = 1;
			t->scroll = 0;
			continue;
		}
		if (n == 0 || (errno != EAGAIN && errno != EINTR)) {
			/* The program exited */
			t->alive = 0;
			waitpid(t->pid, NULL, WNOHANG);
			changed = 1;
		}
		break;
	}
	return changed;
}

void term_write(Term *t, const char *s, int len) {
	if (t->alive && len > 0)
		write(t->fd, s, len);
}

void term_resize(Term *t, int cols, int rows) {
	if (cols < 2) cols = 2;
	if (rows < 2) rows = 2;
	if (cols > TERM_MAX_COLS) cols = TERM_MAX_COLS;
	if (rows > TERM_MAX_ROWS) rows = TERM_MAX_ROWS;
	if (cols == t->cols && rows == t->rows)
		return;
	/* Keep the bottom of the screen, where the cursor usually is */
	if (rows < t->rows && t->cy >= rows) {
		int shift = t->cy - rows + 1;
		for (int i = 0; i < shift; i++)
			push_scrollback(t, t->screen[i]);
		memmove(&t->screen[0], &t->screen[shift], sizeof(t->screen[0]) * (t->rows - shift));
		for (int y = t->rows - shift; y < TERM_MAX_ROWS; y++)
			clear_cells(t, t->screen[y], TERM_MAX_COLS);
		t->cy -= shift;
	}
	t->cols = cols;
	t->rows = rows;
	t->top = 0;
	t->bottom = rows;
	clamp_cursor(t);
	struct winsize ws = {.ws_row = rows, .ws_col = cols};
	if (t->alive)
		ioctl(t->fd, TIOCSWINSZ, &ws);
}

void term_close(Term *t) {
	if (t->alive) {
		kill(t->pid, SIGHUP);
		waitpid(t->pid, NULL, WNOHANG);
	}
	if (t->fd > 0)
		close(t->fd);
	t->alive = 0;
	t->fd = -1;
}

const Cell *term_view_row(Term *t, int y) {
	int back = t->scroll;
	if (y < back) {
		int idx = (t->sb_start + t->sb_count - back + y) % TERM_SCROLLBACK;
		return t->scrollback[idx];
	}
	return t->screen[y - back];
}

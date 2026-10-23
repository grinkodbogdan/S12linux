/* Applications of the desktop. */
#define _GNU_SOURCE
#include <cpuid.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

#include "desk.h"
#include "term.h"

static const mu_Color text_dim = {140, 140, 140, 255};
static const mu_Color text_bright = {222, 222, 222, 255};

/* Card: a rounded panel spanning the layout row */
static mu_Rect card(mu_Context *ctx, int height) {
	mu_layout_row(ctx, 1, (int[]){-1}, height);
	mu_Rect r = mu_layout_next(ctx);
	mu_draw_rect(ctx, r, mu_color(36, 36, 36, 255));
	draw_border(ctx, r, mu_color(22, 22, 22, 255), 0);
	return r;
}

/* ---------------------------------------------------------------- Terminal */

typedef struct {
	Term t;
	int close_requested;
} TermApp;

static const mu_Color palette[17] = {
	{40, 40, 40, 255}, {204, 102, 102, 255}, {152, 190, 101, 255}, {220, 190, 110, 255},
	{110, 150, 190, 255}, {176, 132, 180, 255}, {110, 180, 180, 255}, {200, 200, 200, 255},
	{100, 100, 100, 255}, {230, 120, 120, 255}, {176, 214, 122, 255}, {240, 210, 130, 255},
	{136, 176, 214, 255}, {200, 156, 204, 255}, {136, 204, 204, 255}, {240, 240, 240, 255},
	{210, 210, 210, 255},
};

static void *term_create(Window *win, const char *arg) {
	(void)win;
	(void)arg;
	TermApp *a = calloc(1, sizeof(TermApp));
	char *argv[] = {"/bin/sh", "-l", NULL};
	if (term_spawn(&a->t, 80, 24, argv) < 0)
		a->t.alive = 0;
	return a;
}

static void term_draw(mu_Context *ctx, Window *win) {
	TermApp *a = win->state;
	Term *t = &a->t;
	mu_Container *cnt = mu_get_current_container(ctx);
	if (a->close_requested)
		cnt->open = 0;
	mu_layout_row(ctx, 1, (int[]){-1}, -1);
	mu_Rect r = mu_layout_next(ctx);
	mu_draw_rect(ctx, r, mu_color(24, 24, 24, 255));
	draw_border(ctx, r, mu_color(14, 14, 14, 255), 0);
	int cw = font_mono->glyphs['M' - 32].adv, ch = font_mono->height;
	int cols = (r.w - 16) / cw, rows = (r.h - 12) / ch;
	term_resize(t, cols, rows);
	int ox = r.x + 8, oy = r.y + 6;
	char buf[TERM_MAX_COLS + 1];
	for (int y = 0; y < t->rows; y++) {
		const Cell *row = term_view_row(t, y);
		int x = 0;
		while (x < t->cols) {
			const Cell *c0 = &row[x];
			int start = x;
			int n = 0;
			while (x < t->cols && row[x].fg == c0->fg && row[x].bg == c0->bg && row[x].attr == c0->attr) {
				buf[n++] = row[x].ch ? row[x].ch : ' ';
				x++;
			}
			buf[n] = 0;
			int fg = c0->fg, bg = c0->bg;
			if (c0->attr & ATTR_BOLD && fg < 8)
				fg += 8;
			mu_Color fgc = palette[fg], bgc = bg == 16 ? mu_color(0, 0, 0, 0) : palette[bg];
			if (c0->attr & ATTR_REVERSE) {
				mu_Color tmp = fgc;
				fgc = bg == 16 ? palette[16] : bgc;
				bgc = tmp;
				if (fg == 16)
					bgc = palette[16];
				fgc = bg == 16 ? mu_color(24, 24, 24, 255) : fgc;
			}
			mu_Rect cr = mu_rect(ox + start * cw, oy + y * ch, n * cw, ch);
			if (bgc.a)
				mu_draw_rect(ctx, cr, bgc);
			int blank = 1;
			for (int i = 0; i < n; i++)
				if (buf[i] != ' ')
					blank = 0;
			/* Trailing spaces are not drawn */
			while (n > 0 && buf[n - 1] == ' ')
				buf[--n] = 0;
			if (!blank)
				mu_draw_text(ctx, (mu_Font)font_mono, buf, n, mu_vec2(cr.x, cr.y), fgc);
			if (c0->attr & ATTR_UNDERLINE)
				mu_draw_rect(ctx, mu_rect(cr.x, cr.y + ch - 2, cr.w, 1), fgc);
		}
	}
	if (t->alive && t->cursor_visible && t->scroll == 0) {
		mu_Rect cr = mu_rect(ox + t->cx * cw, oy + t->cy * ch, cw, ch);
		if (win == active_window) {
			mu_draw_rect(ctx, cr, mu_color(210, 210, 210, 255));
			char c = t->screen[t->cy][t->cx].ch;
			if (c && c != ' ')
				mu_draw_text(ctx, (mu_Font)font_mono, &c, 1, mu_vec2(cr.x, cr.y), mu_color(24, 24, 24, 255));
		} else {
			draw_border(ctx, cr, mu_color(170, 170, 170, 255), 0);
		}
	}
	if (!t->alive) {
		const char *msg = "The shell has exited. Press Enter to close this window.";
		int w = font_width(font_ui, msg, -1);
		mu_Rect m = mu_rect(r.x + (r.w - w) / 2 - 16, r.y + r.h - 46, w + 32, 34);
		mu_draw_rect(ctx, m, mu_color(50, 50, 50, 255));
		draw_border(ctx, m, mu_color(20, 20, 20, 255), 0);
		draw_text_font(ctx, font_ui, msg, m.x + 16, m.y + (m.h - font_ui->height) / 2, text_bright);
	}
}

static int term_key(Window *win, const char *s, int len) {
	TermApp *a = win->state;
	if (!a->t.alive) {
		for (int i = 0; i < len; i++)
			if (s[i] == '\r' || s[i] == '\n')
				a->close_requested = 1;
		return 1;
	}
	term_write(&a->t, s, len);
	return 1;
}

static int term_app_poll(Window *win) {
	TermApp *a = win->state;
	return term_poll(&a->t);
}

static void term_destroy(Window *win) {
	TermApp *a = win->state;
	term_close(&a->t);
}


/* ---------------------------------------------------------------- Files */

typedef struct {
	char name[128];
	int dir;
	long size;
} Entry;

typedef struct {
	char path[512];
	Entry *entries;
	int count;
	char error[128];
} FilesApp;

static int entry_cmp(const void *a, const void *b) {
	const Entry *x = a, *y = b;
	if (x->dir != y->dir)
		return y->dir - x->dir;
	return strcmp(x->name, y->name);
}

static void files_load(FilesApp *a) {
	free(a->entries);
	a->entries = NULL;
	a->count = 0;
	a->error[0] = 0;
	DIR *d = opendir(a->path);
	if (!d) {
		snprintf(a->error, sizeof(a->error), "Cannot open this folder");
		return;
	}
	int cap = 0;
	struct dirent *de;
	while ((de = readdir(d))) {
		if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
			continue;
		if (a->count == cap) {
			cap = cap ? cap * 2 : 64;
			a->entries = realloc(a->entries, cap * sizeof(Entry));
		}
		Entry *e = &a->entries[a->count++];
		snprintf(e->name, sizeof(e->name), "%s", de->d_name);
		char full[1024];
		snprintf(full, sizeof(full), "%s/%s", strcmp(a->path, "/") ? a->path : "", de->d_name);
		struct stat st;
		e->dir = stat(full, &st) == 0 && S_ISDIR(st.st_mode);
		e->size = e->dir ? 0 : st.st_size;
	}
	closedir(d);
	qsort(a->entries, a->count, sizeof(Entry), entry_cmp);
}

static void *files_create(Window *win, const char *arg) {
	(void)win;
	FilesApp *a = calloc(1, sizeof(FilesApp));
	snprintf(a->path, sizeof(a->path), "%s", arg ? arg : "/");
	files_load(a);
	return a;
}

static void format_size(long size, char *buf, int n) {
	if (size < 1024)
		snprintf(buf, n, "%ld B", size);
	else if (size < 1024 * 1024)
		snprintf(buf, n, "%.1f KB", size / 1024.0);
	else
		snprintf(buf, n, "%.1f MB", size / (1024.0 * 1024.0));
}

static void files_draw(mu_Context *ctx, Window *win) {
	FilesApp *a = win->state;
	mu_layout_row(ctx, 3, (int[]){36, 36, -1}, 28);
	if (mu_button(ctx, "<") && strcmp(a->path, "/")) {
		char *slash = strrchr(a->path, '/');
		if (slash == a->path)
			slash[1] = 0;
		else if (slash)
			*slash = 0;
		files_load(a);
	}
	if (mu_button(ctx, "~")) {
		snprintf(a->path, sizeof(a->path), "/");
		files_load(a);
	}
	mu_Rect pr = mu_layout_next(ctx);
	mu_draw_rect(ctx, pr, mu_color(30, 30, 30, 255));
	draw_border(ctx, pr, mu_color(18, 18, 18, 255), 0);
	draw_text_font(ctx, font_ui, a->path, pr.x + 12, pr.y + (pr.h - font_ui->height) / 2, text_bright);
	if (a->error[0]) {
		mu_layout_row(ctx, 1, (int[]){-1}, 30);
		label_font(ctx, font_ui, a->error, text_dim);
		return;
	}
	char open_path[1024] = "";
	int open_dir = 0;
	for (int i = 0; i < a->count; i++) {
		Entry *e = &a->entries[i];
		mu_layout_row(ctx, 1, (int[]){-1}, 24);
		mu_Rect r = mu_layout_next(ctx);
		mu_Id id = mu_get_id(ctx, e->name, strlen(e->name));
		mu_update_control(ctx, id, r, 0);
		if (ctx->hover == id)
			mu_draw_rect(ctx, r, color_alpha(accent, 160));
		else if (i % 2)
			mu_draw_rect(ctx, r, mu_color(255, 255, 255, 6));
		draw_appicon(ctx, mu_rect(r.x + 8, r.y + 4, 16, 16), e->dir ? APP_FILES : APP_VIEWER,
			e->dir ? mu_color(200, 160, 90, 255) : mu_color(130, 140, 155, 255));
		draw_text_font(ctx, font_ui, e->name, r.x + 36, r.y + (r.h - font_ui->height) / 2, text_bright);
		char size[32] = "Folder";
		if (!e->dir)
			format_size(e->size, size, sizeof(size));
		int sw = font_width(font_small, size, -1);
		draw_text_font(ctx, font_small, size, r.x + r.w - sw - 12, r.y + (r.h - font_small->height) / 2,
			text_dim);
		if (ctx->mouse_pressed == MU_MOUSE_LEFT && ctx->focus == id) {
			snprintf(open_path, sizeof(open_path), "%s/%s", strcmp(a->path, "/") ? a->path : "", e->name);
			open_dir = e->dir;
		}
	}
	if (open_path[0]) {
		if (open_dir) {
			snprintf(a->path, sizeof(a->path), "%s", open_path);
			files_load(a);
		} else {
			open_app(APP_VIEWER, open_path);
		}
	}
}

static void files_destroy(Window *win) {
	FilesApp *a = win->state;
	free(a->entries);
}

/* ---------------------------------------------------------------- Viewer */

#define VIEWER_MAX (64 * 1024)

typedef struct {
	char path[512];
	char *text;
	int len;
	int binary;
} ViewerApp;

static void *viewer_create(Window *win, const char *arg) {
	ViewerApp *a = calloc(1, sizeof(ViewerApp));
	snprintf(a->path, sizeof(a->path), "%s", arg ? arg : "");
	const char *base = strrchr(a->path, '/');
	snprintf(win->title, sizeof(win->title), "%s", base ? base + 1 : a->path);
	a->text = malloc(VIEWER_MAX + 1);
	a->len = read_file(a->path, a->text, VIEWER_MAX + 1);
	if (a->len < 0)
		a->len = 0;
	for (int i = 0; i < a->len; i++)
		if (a->text[i] == 0 || ((unsigned char)a->text[i] < 9))
			a->binary = 1;
	return a;
}

static void viewer_draw(mu_Context *ctx, Window *win) {
	ViewerApp *a = win->state;
	mu_layout_row(ctx, 1, (int[]){-1}, 20);
	label_font(ctx, font_small, a->path, text_dim);
	if (a->binary) {
		mu_layout_row(ctx, 1, (int[]){-1}, 30);
		label_font(ctx, font_ui, "This is a binary file.", text_bright);
		return;
	}
	char *p = a->text, *end = a->text + a->len;
	int lines = 0;
	while (p < end && lines < 3000) {
		char *nl = memchr(p, '\n', end - p);
		int n = nl ? nl - p : end - p;
		mu_layout_row(ctx, 1, (int[]){-1}, font_mono->height);
		mu_Rect r = mu_layout_next(ctx);
		if (n > 0) {
			char line[512];
			int k = 0;
			for (int i = 0; i < n && k < (int)sizeof(line) - 8; i++) {
				if (p[i] == '\t') {
					do line[k++] = ' '; while (k % 4);
				} else if (p[i] != '\r') {
					line[k++] = p[i];
				}
			}
			mu_draw_text(ctx, (mu_Font)font_mono, line, k, mu_vec2(r.x, r.y), mu_color(214, 218, 230, 255));
		}
		p += n + 1;
		lines++;
	}
}

static void viewer_destroy(Window *win) {
	ViewerApp *a = win->state;
	free(a->text);
}

/* ---------------------------------------------------------------- System monitor */

#define HISTORY 60

typedef struct {
	long total, used;
	float history[HISTORY];
	int samples;
	long last;
	char uptime[64];
	struct {
		int pid;
		char name[32];
		char state[16];
	} procs[64];
	int nprocs;
} MonitorApp;

static void monitor_sample(MonitorApp *a) {
	char buf[1024];
	if (read_file("/proc/meminfo", buf, sizeof(buf)) > 0) {
		char *p = strstr(buf, "MemTotal:");
		long total = p ? strtol(p + 9, NULL, 10) : 0;
		p = strstr(buf, "MemFree:");
		long free_kb = p ? strtol(p + 8, NULL, 10) : 0;
		a->total = total;
		a->used = total - free_kb;
	}
	memmove(a->history, a->history + 1, sizeof(float) * (HISTORY - 1));
	a->history[HISTORY - 1] = a->total ? (float)a->used / a->total : 0;
	if (a->samples < HISTORY)
		a->samples++;
	if (read_file("/proc/uptime", buf, sizeof(buf)) > 0) {
		long s = strtol(buf, NULL, 10);
		snprintf(a->uptime, sizeof(a->uptime), "%ldh %02ldm %02lds", s / 3600, s / 60 % 60, s % 60);
	}
	a->nprocs = 0;
	DIR *d = opendir("/proc");
	struct dirent *de;
	while (d && (de = readdir(d)) && a->nprocs < 64) {
		int pid = atoi(de->d_name);
		if (pid <= 0)
			continue;
		char path[64];
		snprintf(path, sizeof(path), "/proc/%d/status", pid);
		if (read_file(path, buf, sizeof(buf)) <= 0)
			continue;
		a->procs[a->nprocs].pid = pid;
		char *name = strstr(buf, "Name:");
		char *state = strstr(buf, "State:");
		sscanf(name ? name + 5 : "?", " %31[^\n]", a->procs[a->nprocs].name);
		sscanf(state ? state + 6 : "?", " %*c (%15[^)])", a->procs[a->nprocs].state);
		a->nprocs++;
	}
	if (d)
		closedir(d);
}

static void *monitor_create(Window *win, const char *arg) {
	(void)win;
	(void)arg;
	MonitorApp *a = calloc(1, sizeof(MonitorApp));
	monitor_sample(a);
	a->last = now_ms();
	return a;
}

static int monitor_poll(Window *win) {
	MonitorApp *a = win->state;
	if (now_ms() - a->last < 1000)
		return 0;
	a->last = now_ms();
	monitor_sample(a);
	return 1;
}

static void monitor_draw(mu_Context *ctx, Window *win) {
	MonitorApp *a = win->state;
	char buf[96];
	/* Memory card with a history graph */
	mu_Rect c = card(ctx, 150);
	draw_text_font(ctx, font_small, "MEMORY", c.x + 16, c.y + 12, text_dim);
	snprintf(buf, sizeof(buf), "%ld MB", a->used / 1024);
	draw_text_font(ctx, font_big, buf, c.x + 16, c.y + 30, text_bright);
	int bw = font_width(font_big, buf, -1);
	snprintf(buf, sizeof(buf), "of %ld MB", a->total / 1024);
	draw_text_font(ctx, font_ui, buf, c.x + 24 + bw, c.y + 42, text_dim);
	mu_Rect g = mu_rect(c.x + 16, c.y + 78, c.w - 32, 56);
	mu_draw_rect(ctx, g, mu_color(24, 24, 24, 255));
	draw_border(ctx, g, mu_color(18, 18, 18, 255), 0);
	int bars = HISTORY;
	float step = (float)g.w / bars;
	for (int i = HISTORY - a->samples; i < HISTORY; i++) {
		int h = (int)(a->history[i] * (g.h - 8));
		if (h < 2)
			h = 2;
		mu_Rect b = mu_rect(g.x + (int)(i * step) + 1, g.y + g.h - 4 - h, (int)step - 2 > 1 ? (int)step - 2 : 1, h);
		mu_draw_rect(ctx, b, color_alpha(accent, 140 + i * 115 / HISTORY));
	}

	/* System card */
	mu_Rect s = card(ctx, 64);
	draw_text_font(ctx, font_small, "UPTIME", s.x + 16, s.y + 12, text_dim);
	draw_text_font(ctx, font_bold, a->uptime, s.x + 16, s.y + 32, text_bright);
	draw_text_font(ctx, font_small, "PROCESSES", s.x + s.w / 2, s.y + 12, text_dim);
	snprintf(buf, sizeof(buf), "%d", a->nprocs);
	draw_text_font(ctx, font_bold, buf, s.x + s.w / 2, s.y + 32, text_bright);

	/* Process list */
	mu_layout_row(ctx, 3, (int[]){70, -110, -1}, 22);
	label_font(ctx, font_small, "PID", text_dim);
	label_font(ctx, font_small, "NAME", text_dim);
	label_font(ctx, font_small, "STATE", text_dim);
	for (int i = 0; i < a->nprocs; i++) {
		mu_layout_row(ctx, 3, (int[]){70, -110, -1}, 24);
		snprintf(buf, sizeof(buf), "%d", a->procs[i].pid);
		label_font(ctx, font_ui, buf, text_dim);
		label_font(ctx, font_ui, a->procs[i].name, text_bright);
		label_font(ctx, font_ui, a->procs[i].state, text_dim);
	}
}

/* ---------------------------------------------------------------- Calculator */

typedef struct {
	char display[32];
	double acc;
	char op;
	int fresh;   /* the next digit starts a new number */
} CalcApp;

static void *calc_create(Window *win, const char *arg) {
	(void)win;
	(void)arg;
	CalcApp *a = calloc(1, sizeof(CalcApp));
	strcpy(a->display, "0");
	a->fresh = 1;
	return a;
}

static void calc_show(CalcApp *a, double v) {
	snprintf(a->display, sizeof(a->display), "%.10g", v);
}

static void calc_apply(CalcApp *a) {
	double v = atof(a->display);
	switch (a->op) {
	case '+': a->acc += v; break;
	case '-': a->acc -= v; break;
	case '*': a->acc *= v; break;
	case '/': a->acc = v != 0 ? a->acc / v : 0; break;
	default: a->acc = v; break;
	}
	calc_show(a, a->acc);
}

static void calc_press(CalcApp *a, const char *k) {
	char c = k[0];
	if ((c >= '0' && c <= '9') || c == '.') {
		if (a->fresh) {
			strcpy(a->display, c == '.' ? "0" : "");
			a->fresh = 0;
		}
		if (c == '.' && strchr(a->display, '.'))
			return;
		if (strlen(a->display) < 16)
			strncat(a->display, k, 1);
	} else if (c == 'C') {
		strcpy(a->display, "0");
		a->acc = 0;
		a->op = 0;
		a->fresh = 1;
	} else if (c == '=') {
		calc_apply(a);
		a->op = 0;
		a->fresh = 1;
	} else if (!strcmp(k, "+/-")) {
		calc_show(a, -atof(a->display));
	} else if (c == '%') {
		calc_show(a, atof(a->display) / 100);
	} else {
		if (!a->fresh)
			calc_apply(a);
		a->op = c == 'x' ? '*' : (c == ':' ? '/' : c);
		a->fresh = 1;
	}
}

static void calc_draw(mu_Context *ctx, Window *win) {
	CalcApp *a = win->state;
	mu_Rect d = card(ctx, 70);
	int w = font_width(font_big, a->display, -1);
	draw_text_font(ctx, font_big, a->display, d.x + d.w - w - 16, d.y + (d.h - font_big->height) / 2,
		text_bright);
	static const char *keys[] = {"C", "+/-", "%", ":", "7", "8", "9", "x", "4", "5", "6", "-",
		"1", "2", "3", "+", "0", ".", "="};
	int bw = (mu_get_current_container(ctx)->body.w - 14 - 3 * 6) / 4;
	for (int i = 0; i < 19; i++) {
		if (i % 4 == 0 || i == 16) {
			if (i == 16)
				mu_layout_row(ctx, 3, (int[]){bw * 2 + 6, bw, bw}, 46);
			else
				mu_layout_row(ctx, 4, (int[]){bw, bw, bw, bw}, 46);
		}
		const char *k = keys[i];
		int is_op = i % 4 == 3 || i == 18;
		mu_Id id = mu_get_id(ctx, k, strlen(k));
		mu_Rect r = mu_layout_next(ctx);
		mu_update_control(ctx, id, r, 0);
		mu_Color bg = is_op ? color_mix(mu_color(60, 60, 60, 255), accent, 200)
			: (i < 3 ? mu_color(70, 70, 70, 255) : mu_color(58, 58, 58, 255));
		if (ctx->hover == id)
			bg = color_mix(bg, mu_color(255, 255, 255, 255), 25);
		draw_rrect(ctx, r, mu_color(18, 18, 18, 255), 2);
		draw_rrect(ctx, mu_rect(r.x + 1, r.y + 1, r.w - 2, r.h - 2), bg, 1);
		int tw = font_width(font_bold, k, -1);
		draw_text_font(ctx, font_bold, !strcmp(k, ":") ? "/" : k, r.x + (r.w - tw) / 2,
			r.y + (r.h - font_bold->height) / 2, text_bright);
		if (ctx->mouse_pressed == MU_MOUSE_LEFT && ctx->focus == id)
			calc_press(a, k);
	}
}

static int calc_key(Window *win, const char *s, int len) {
	CalcApp *a = win->state;
	for (int i = 0; i < len; i++) {
		char c = s[i];
		char k[2] = {c, 0};
		if ((c >= '0' && c <= '9') || c == '.' || c == '+' || c == '-' || c == '%')
			calc_press(a, k);
		else if (c == '*')
			calc_press(a, "x");
		else if (c == '/')
			calc_press(a, ":");
		else if (c == '\r' || c == '\n' || c == '=')
			calc_press(a, "=");
		else if (c == 27 || c == 'c' || c == 127)
			calc_press(a, "C");
	}
	return 1;
}

/* ---------------------------------------------------------------- Minesweeper */

#define MINES_N 9
#define MINES_COUNT 10

typedef struct {
	unsigned char mine[MINES_N][MINES_N], open[MINES_N][MINES_N], flag[MINES_N][MINES_N];
	int state;   /* 0 playing, 1 lost, 2 won */
	int started;
} MinesApp;

static void mines_reset(MinesApp *a) {
	memset(a, 0, sizeof(*a));
}

static void mines_place(MinesApp *a, int sx, int sy) {
	srand(time(NULL) ^ (sx * 31 + sy));
	int placed = 0;
	while (placed < MINES_COUNT) {
		int x = rand() % MINES_N, y = rand() % MINES_N;
		if (a->mine[y][x] || (abs(x - sx) <= 1 && abs(y - sy) <= 1))
			continue;
		a->mine[y][x] = 1;
		placed++;
	}
	a->started = 1;
}

static int mines_around(MinesApp *a, int x, int y) {
	int n = 0;
	for (int dy = -1; dy <= 1; dy++)
		for (int dx = -1; dx <= 1; dx++) {
			int nx = x + dx, ny = y + dy;
			if (nx >= 0 && ny >= 0 && nx < MINES_N && ny < MINES_N)
				n += a->mine[ny][nx];
		}
	return n;
}

static void mines_open(MinesApp *a, int x, int y) {
	if (x < 0 || y < 0 || x >= MINES_N || y >= MINES_N || a->open[y][x] || a->flag[y][x])
		return;
	a->open[y][x] = 1;
	if (a->mine[y][x]) {
		a->state = 1;
		return;
	}
	if (mines_around(a, x, y) == 0)
		for (int dy = -1; dy <= 1; dy++)
			for (int dx = -1; dx <= 1; dx++)
				mines_open(a, x + dx, y + dy);
}

static void *mines_create(Window *win, const char *arg) {
	(void)win;
	(void)arg;
	return calloc(1, sizeof(MinesApp));
}

static void mines_draw(mu_Context *ctx, Window *win) {
	MinesApp *a = win->state;
	int closed = 0, flags = 0;
	for (int y = 0; y < MINES_N; y++)
		for (int x = 0; x < MINES_N; x++) {
			closed += !a->open[y][x];
			flags += a->flag[y][x];
		}
	if (a->state == 0 && a->started && closed == MINES_COUNT)
		a->state = 2;
	mu_layout_row(ctx, 2, (int[]){-110, -1}, 30);
	const char *status = a->state == 1 ? "Boom! You hit a mine." : (a->state == 2 ? "You won!" : "");
	char buf[64];
	if (!status[0]) {
		snprintf(buf, sizeof(buf), "Mines left: %d", MINES_COUNT - flags);
		status = buf;
	}
	label_font(ctx, font_bold, status, a->state == 1 ? mu_color(255, 120, 130, 255) : text_bright);
	if (mu_button(ctx, "New game"))
		mines_reset(a);
	int size = 36;
	int widths[MINES_N];
	for (int i = 0; i < MINES_N; i++)
		widths[i] = size;
	static const mu_Color numbers[9] = {{0}, {120, 160, 210, 255}, {150, 190, 110, 255},
		{220, 110, 110, 255}, {170, 130, 190, 255}, {210, 170, 100, 255}, {110, 180, 180, 255},
		{230, 230, 230, 255}, {170, 170, 170, 255}};
	for (int y = 0; y < MINES_N; y++) {
		mu_layout_row(ctx, MINES_N, widths, size);
		for (int x = 0; x < MINES_N; x++) {
			int key = y * MINES_N + x;
			mu_Id id = mu_get_id(ctx, &key, sizeof(key));
			mu_Rect r = mu_layout_next(ctx);
			mu_update_control(ctx, id, r, 0);
			int shown = a->open[y][x] || (a->state == 1 && a->mine[y][x]);
			mu_Color bg = shown ? mu_color(34, 34, 34, 255) : mu_color(66, 66, 66, 255);
			if (!shown && ctx->hover == id && a->state == 0)
				bg = mu_color(80, 80, 80, 255);
			if (shown && a->mine[y][x])
				bg = mu_color(170, 60, 60, 255);
			mu_draw_rect(ctx, r, mu_color(20, 20, 20, 255));
			mu_draw_rect(ctx, mu_rect(r.x + 1, r.y + 1, r.w - 2, r.h - 2), bg);
			if (!shown)
				mu_draw_rect(ctx, mu_rect(r.x + 1, r.y + 1, r.w - 2, 1), mu_color(255, 255, 255, 22));
			const char *label = NULL;
			mu_Color lc = text_bright;
			char num[2] = {0};
			if (shown && a->mine[y][x]) {
				label = "*";
			} else if (shown) {
				int n = mines_around(a, x, y);
				if (n) {
					num[0] = '0' + n;
					label = num;
					lc = numbers[n];
				}
			} else if (a->flag[y][x]) {
				label = "F";
				lc = mu_color(255, 200, 90, 255);
			}
			if (label) {
				int tw = font_width(font_bold, label, -1);
				draw_text_font(ctx, font_bold, label, r.x + (r.w - tw) / 2, r.y + (r.h - font_bold->height) / 2, lc);
			}
			if (a->state == 0 && ctx->focus == id && ctx->mouse_pressed) {
				if (ctx->mouse_pressed & MU_MOUSE_RIGHT) {
					if (!a->open[y][x])
						a->flag[y][x] = !a->flag[y][x];
				} else {
					if (!a->started)
						mines_place(a, x, y);
					mines_open(a, x, y);
				}
			}
		}
	}
	mu_layout_row(ctx, 1, (int[]){-1}, 20);
	label_font(ctx, font_small, "Left click to open, right click to place a flag.", text_dim);
}

/* ---------------------------------------------------------------- Settings */

static void *settings_create(Window *win, const char *arg) {
	(void)win;
	(void)arg;
	return calloc(1, 1);
}

/* Reads a number from a sysfs attribute, or returns -1 */
static long read_long(const char *path) {
	char buf[32];
	if (read_file(path, buf, sizeof(buf)) <= 0)
		return -1;
	return strtol(buf, NULL, 10);
}

/* Writes a string to a sysfs attribute */
static void write_attr(const char *path, const char *value) {
	int fd = open(path, O_WRONLY);
	if (fd < 0)
		return;
	write(fd, value, strlen(value));
	close(fd);
}

#define BACKLIGHT "/sys/class/backlight/intel_backlight/"
#define CPUFREQ "/sys/devices/system/cpu/cpu0/cpufreq/"
#define MIXER_READ_VOLUME 0x80044d00
#define MIXER_WRITE_VOLUME 0xc0044d00

/* Brightness, volume and processor speed, for the hardware that has them */
static void settings_hardware(mu_Context *ctx) {
	static float brightness = -1, volume = -1;
	static long max_brightness = -2;
	static int mixer = -2;
	if (max_brightness == -2) {
		max_brightness = read_long(BACKLIGHT "max_brightness");
		long b = read_long(BACKLIGHT "brightness");
		if (max_brightness > 0 && b >= 0)
			brightness = 100.f * b / max_brightness;
	}
	if (mixer == -2) {
		mixer = open("/dev/mixer", O_RDWR);
		int v = 0;
		if (mixer >= 0 && ioctl(mixer, MIXER_READ_VOLUME, &v) == 0)
			volume = v & 0xff;
	}
	/* Only with frequency scaling, where the frequency range is not a single value */
	int has_freq = read_long(CPUFREQ "cpuinfo_max_freq") > read_long(CPUFREQ "cpuinfo_min_freq");
	if (brightness < 0 && volume < 0 && !has_freq)
		return;
	mu_layout_row(ctx, 1, (int[]){-1}, 12);
	mu_layout_next(ctx);
	mu_layout_row(ctx, 1, (int[]){-1}, 24);
	label_font(ctx, font_bold, "Hardware", text_bright);
	if (brightness >= 0) {
		mu_layout_row(ctx, 2, (int[]){110, -1}, 24);
		mu_label(ctx, "Brightness");
		if (mu_slider_ex(ctx, &brightness, 5, 100, 1, "%.0f%%", MU_OPT_ALIGNCENTER) & MU_RES_CHANGE) {
			char v[24];
			snprintf(v, sizeof(v), "%ld", (long)(brightness * max_brightness / 100));
			write_attr(BACKLIGHT "brightness", v);
		}
	}
	if (volume >= 0) {
		mu_layout_row(ctx, 2, (int[]){110, -1}, 24);
		mu_label(ctx, "Volume");
		if (mu_slider_ex(ctx, &volume, 0, 100, 1, "%.0f%%", MU_OPT_ALIGNCENTER) & MU_RES_CHANGE) {
			int v = (int)volume | ((int)volume << 8);
			ioctl(mixer, MIXER_WRITE_VOLUME, &v);
		}
	}
	if (has_freq) {
		static const char *govs[3] = {"ondemand", "performance", "powersave"};
		static const char *labels[3] = {"Automatic", "Fast", "Battery"};
		char cur[32] = "";
		read_file(CPUFREQ "scaling_governor", cur, sizeof(cur));
		mu_layout_row(ctx, 4, (int[]){110, 90, 90, 90}, 26);
		mu_label(ctx, "Processor");
		for (int i = 0; i < 3; i++) {
			int selected = !strncmp(cur, govs[i], strlen(govs[i]));
			if (selected)
				ctx->style->colors[MU_COLOR_BUTTON] = color_mix(mu_color(60, 60, 60, 255), accent, 200);
			if (mu_button(ctx, labels[i]) && !selected)
				write_attr(CPUFREQ "scaling_governor", govs[i]);
			if (selected)
				ctx->style->colors[MU_COLOR_BUTTON] = mu_color(60, 60, 60, 255);
		}
	}
}

static void settings_draw(mu_Context *ctx, Window *win) {
	(void)win;
	static const mu_Color accents[6] = {{95, 129, 157, 255}, {196, 120, 60, 255}, {110, 140, 90, 255},
		{160, 80, 80, 255}, {125, 105, 150, 255}, {110, 110, 110, 255}};
	mu_layout_row(ctx, 1, (int[]){-1}, 24);
	label_font(ctx, font_bold, "Accent color", text_bright);
	mu_layout_row(ctx, 6, (int[]){38, 38, 38, 38, 38, 38}, 30);
	for (int i = 0; i < 6; i++) {
		mu_Id id = mu_get_id(ctx, &accents[i], sizeof(mu_Color));
		mu_Rect r = mu_layout_next(ctx);
		mu_update_control(ctx, id, r, 0);
		int selected = !memcmp(&accent, &accents[i], sizeof(mu_Color));
		mu_draw_rect(ctx, r, selected ? mu_color(230, 230, 230, 255)
			: (ctx->hover == id ? mu_color(120, 120, 120, 255) : mu_color(18, 18, 18, 255)));
		mu_draw_rect(ctx, mu_rect(r.x + 2, r.y + 2, r.w - 4, r.h - 4), accents[i]);
		if (ctx->mouse_pressed == MU_MOUSE_LEFT && ctx->focus == id && !selected) {
			accent = accents[i];
			apply_theme(ctx);
			force_redraw();
		}
	}
	mu_layout_row(ctx, 1, (int[]){-1}, 12);
	mu_layout_next(ctx);
	mu_layout_row(ctx, 1, (int[]){-1}, 24);
	label_font(ctx, font_bold, "Wallpaper", text_bright);
	static const char *styles[3] = {"Graphite", "Slate", "Plain"};
	mu_layout_row(ctx, 3, (int[]){100, 100, 100}, 26);
	for (int i = 0; i < 3; i++) {
		int selected = wallpaper_style == i;
		if (selected)
			ctx->style->colors[MU_COLOR_BUTTON] = color_mix(mu_color(60, 60, 60, 255), accent, 200);
		if (mu_button(ctx, styles[i]) && !selected) {
			wallpaper_style = i;
			force_redraw();
		}
		if (selected)
			ctx->style->colors[MU_COLOR_BUTTON] = mu_color(60, 60, 60, 255);
	}
	mu_layout_row(ctx, 1, (int[]){-1}, 12);
	mu_layout_next(ctx);
	mu_layout_row(ctx, 1, (int[]){-1}, 24);
	label_font(ctx, font_bold, "Clock", text_bright);
	mu_layout_row(ctx, 1, (int[]){-1}, 28);
	mu_checkbox(ctx, "Use the 24-hour format", &clock_24h);
	mu_layout_row(ctx, 1, (int[]){-1}, 12);
	mu_layout_next(ctx);
	mu_layout_row(ctx, 1, (int[]){-1}, 24);
	label_font(ctx, font_bold, "Turn the screen off after", text_bright);
	static const int minutes[4] = {2, 5, 10, 0};
	static const char *labels[4] = {"2 minutes", "5 minutes", "10 minutes", "Never"};
	mu_layout_row(ctx, 4, (int[]){90, 90, 90, 90}, 26);
	for (int i = 0; i < 4; i++) {
		int selected = blank_minutes == minutes[i];
		if (selected)
			ctx->style->colors[MU_COLOR_BUTTON] = color_mix(mu_color(60, 60, 60, 255), accent, 200);
		if (mu_button(ctx, labels[i]))
			blank_minutes = minutes[i];
		if (selected)
			ctx->style->colors[MU_COLOR_BUTTON] = mu_color(60, 60, 60, 255);
	}
	settings_hardware(ctx);
}

/* ---------------------------------------------------------------- About */

typedef struct {
	char kernel[96];
	char cpu[64];
	char memory[96];
	char display[64];
	char graphics[48];
} AboutApp;

static void *about_create(Window *win, const char *arg) {
	(void)win;
	(void)arg;
	AboutApp *a = calloc(1, sizeof(AboutApp));
	struct utsname u;
	if (uname(&u) == 0)
		snprintf(a->kernel, sizeof(a->kernel), "%s %s (%s)", u.sysname, u.release, u.machine);
	unsigned int regs[12];
	if (__get_cpuid(0x80000000, &regs[0], &regs[1], &regs[2], &regs[3]) && regs[0] >= 0x80000004) {
		for (int i = 0; i < 3; i++)
			__get_cpuid(0x80000002 + i, &regs[i * 4], &regs[i * 4 + 1], &regs[i * 4 + 2], &regs[i * 4 + 3]);
		char brand[49];
		memcpy(brand, regs, 48);
		brand[48] = 0;
		char *b = brand;
		while (*b == ' ')
			b++;
		/* Collapse repeated spaces */
		char *o = a->cpu;
		for (char *p = b; *p && o < a->cpu + sizeof(a->cpu) - 1; p++)
			if (!(*p == ' ' && p[1] == ' '))
				*o++ = *p;
	} else {
		snprintf(a->cpu, sizeof(a->cpu), "x86 processor");
	}
	char buf[256];
	if (read_file("/proc/meminfo", buf, sizeof(buf)) > 0) {
		char *p = strstr(buf, "MemTotal:");
		snprintf(a->memory, sizeof(a->memory), "%ld MB", p ? strtol(p + 9, NULL, 10) / 1024 : 0);
	}
	/* Memory modules, as "DDR2-667", from their SPD data: "slot 0: DDR2 1024 MiB, DDR2-667 part" */
	char dimms[512];
	if (read_file("/proc/dimms", dimms, sizeof(dimms)) > 0) {
		int count = 0;
		char kind[24] = "";
		for (char *l = dimms; l && *l; l = strchr(l, '\n') ? strchr(l, '\n') + 1 : NULL) {
			char *k = strchr(l, ',');
			if (k && sscanf(k + 1, " %23s", kind) == 1)
				count++;
		}
		if (count)
			snprintf(a->memory + strlen(a->memory), sizeof(a->memory) - strlen(a->memory), ", %d x %s", count,
				kind);
	}
	snprintf(a->display, sizeof(a->display), "%d x %d", scr.w, scr.h);
	if (read_file("/sys/class/graphics/fb0/name", a->graphics, sizeof(a->graphics)) > 0)
		a->graphics[strcspn(a->graphics, "\n")] = 0;
	else
		snprintf(a->graphics, sizeof(a->graphics), "VESA framebuffer");
	return a;
}

static void about_draw(mu_Context *ctx, Window *win) {
	AboutApp *a = win->state;
	mu_layout_row(ctx, 1, (int[]){-1}, 110);
	mu_Rect r = mu_layout_next(ctx);
	int s = 64;
	draw_appicon(ctx, mu_rect(r.x + (r.w - s) / 2, r.y + 6, s, s), -1, accent);
	int tw = font_width(font_big, "S12 Linux", -1);
	draw_text_font(ctx, font_big, "S12 Linux", r.x + (r.w - tw) / 2, r.y + 72, text_bright);
	const char *sub = "A small Linux system for the IdeaPad S12";
	tw = font_width(font_small, sub, -1);
	mu_layout_row(ctx, 1, (int[]){-1}, 22);
	mu_Rect sr = mu_layout_next(ctx);
	draw_text_font(ctx, font_small, sub, sr.x + (sr.w - tw) / 2, sr.y + 2, text_dim);
	const char *labels[] = {"Kernel", "Processor", "Memory", "Graphics", "Display", "Shell", "Desktop"};
	const char *values[] = {a->kernel, a->cpu, a->memory, a->graphics, a->display, "hush",
		"S12 Desk (microui " MU_VERSION ")"};
	mu_Rect c = card(ctx, 7 * 30 + 16);
	for (int i = 0; i < 7; i++) {
		int y = c.y + 10 + i * 30;
		draw_text_font(ctx, font_ui, labels[i], c.x + 16, y + 6, text_dim);
		draw_text_font(ctx, font_ui, values[i], c.x + 120, y + 6, text_bright);
		if (i < 6)
			mu_draw_rect(ctx, mu_rect(c.x + 16, y + 29, c.w - 32, 1), mu_color(255, 255, 255, 12));
	}
}

/* ---------------------------------------------------------------- Registry */

const App apps[APP_COUNT] = {
	[APP_TERMINAL] = {"Terminal", "A shell", 700, 440, {70, 70, 70, 255}, 1, term_create, term_draw, term_destroy,
		term_key, term_app_poll},
	[APP_FILES] = {"Files", "Browse files", 520, 480, {190, 150, 80, 255}, 1, files_create, files_draw,
		files_destroy, NULL, NULL},
	[APP_MONITOR] = {"Monitor", "System monitor", 460, 520, {90, 140, 100, 255}, 1, monitor_create, monitor_draw,
		NULL, NULL, monitor_poll},
	[APP_CALC] = {"Calculator", "Calculator", 300, 470, {170, 110, 60, 255}, 1, calc_create, calc_draw, NULL,
		calc_key, NULL},
	[APP_MINES] = {"Mines", "Minesweeper", 400, 500, {150, 80, 90, 255}, 1, mines_create, mines_draw, NULL, NULL,
		NULL},
	[APP_SETTINGS] = {"Settings", "Appearance and hardware", 420, 560, {100, 105, 115, 255}, 1, settings_create, settings_draw,
		NULL, NULL, NULL},
	[APP_ABOUT] = {"About", "About this system", 460, 450, {95, 129, 157, 255}, 1, about_create, about_draw, NULL,
		NULL, NULL},
	[APP_VIEWER] = {"Viewer", "Text viewer", 620, 460, {120, 125, 135, 255}, 0, viewer_create, viewer_draw,
		viewer_destroy, NULL, NULL},
};

/* Adds the file descriptors of the terminals to wait on */
int app_fds(fd_set *fds, int maxfd) {
	extern Window *desk_windows(int *count);
	int count;
	Window *wins = desk_windows(&count);
	for (int i = 0; i < count; i++) {
		if (!wins[i].used || wins[i].app != APP_TERMINAL || !wins[i].state)
			continue;
		TermApp *a = wins[i].state;
		if (a->t.alive && a->t.fd >= 0) {
			FD_SET(a->t.fd, fds);
			if (a->t.fd > maxfd)
				maxfd = a->t.fd;
		}
	}
	return maxfd;
}

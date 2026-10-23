/*
 * S12 Desk: a small desktop for S12 Linux, built with microui.
 *
 * Everything runs in one process: microui builds the interface each frame, and a software
 * renderer draws its command list into a back buffer. The screen is divided into cells, each
 * with a hash of the commands covering it, so that only the cells that changed since the last
 * frame are redrawn and copied to the framebuffer.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/utsname.h>
#include <sys/wait.h>

#include "desk.h"

#define PANEL_H 30
#define MAX_WINDOWS 16
#define CELL 32

static mu_Context *ctx;
mu_Color accent = {95, 129, 157, 255};
int wallpaper_style = 0;
int clock_24h = 1;
int blank_minutes = 10;
mu_Vec2 mouse_pos;
int frame_counter;
Window *active_window;

static Window windows[MAX_WINDOWS];
static int menu_open;
static int pending_keyup;
static int full_redraw = 1;
static int mouse_buttons;

long now_ms(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000L + ts.tv_nsec / 1000000;
}

mu_Color color_alpha(mu_Color c, int a) {
	c.a = a;
	return c;
}

mu_Color color_mix(mu_Color a, mu_Color b, int t) {
	return mu_color(a.r + (b.r - a.r) * t / 255, a.g + (b.g - a.g) * t / 255,
		a.b + (b.b - a.b) * t / 255, a.a + (b.a - a.a) * t / 255);
}

void force_redraw(void) {
	full_redraw = 1;
}

int read_file(const char *path, char *buf, int size) {
	int fd = open(path, O_RDONLY);
	if (fd < 0)
		return -1;
	int len = 0;
	while (len < size - 1) {
		int n = read(fd, buf + len, size - 1 - len);
		if (n <= 0)
			break;
		len += n;
	}
	close(fd);
	buf[len] = 0;
	return len;
}

/* --- Custom commands --- */

static ShapeCommand *push_shape(mu_Context *ctx, int type, mu_Rect r) {
	if (mu_check_clip(ctx, r) == MU_CLIP_ALL)
		return NULL;
	ShapeCommand *cmd = (ShapeCommand *)mu_push_command(ctx, type, sizeof(ShapeCommand));
	memset((char *)cmd + sizeof(mu_BaseCommand), 0, sizeof(*cmd) - sizeof(mu_BaseCommand));
	cmd->rect = r;
	return cmd;
}

void draw_rrect(mu_Context *ctx, mu_Rect r, mu_Color c, int radius) {
	ShapeCommand *cmd = push_shape(ctx, CMD_RRECT, r);
	if (cmd) {
		cmd->color = c;
		cmd->radius = radius;
	}
}

void draw_border(mu_Context *ctx, mu_Rect r, mu_Color c, int radius) {
	ShapeCommand *cmd = push_shape(ctx, CMD_BORDER, r);
	if (cmd) {
		cmd->color = c;
		cmd->radius = radius;
	}
}

void draw_gradient(mu_Context *ctx, mu_Rect r, mu_Color top, mu_Color bottom, int radius) {
	ShapeCommand *cmd = push_shape(ctx, CMD_GRADIENT, r);
	if (cmd) {
		cmd->color = top;
		cmd->color2 = bottom;
		cmd->radius = radius;
	}
}

static void draw_shadow(mu_Context *ctx, mu_Rect r, int radius, int spread, int alpha, int dy) {
	mu_Rect bounds = mu_rect(r.x - spread, r.y - spread + dy, r.w + spread * 2, r.h + spread * 2);
	ShapeCommand *cmd = push_shape(ctx, CMD_SHADOW, bounds);
	if (cmd) {
		cmd->rect = r;
		cmd->radius = radius;
		cmd->spread = spread;
		cmd->alpha = alpha;
		cmd->dy = dy;
	}
}

void draw_appicon(mu_Context *ctx, mu_Rect r, int kind, mu_Color color) {
	ShapeCommand *cmd = push_shape(ctx, CMD_APPICON, r);
	if (cmd) {
		cmd->color = color;
		cmd->kind = kind;
	}
}

void draw_text_font(mu_Context *ctx, const FontData *f, const char *s, int x, int y, mu_Color c) {
	mu_draw_text(ctx, (mu_Font)f, s, -1, mu_vec2(x, y), c);
}

/* --- Theme --- */

/* Flat, dark grey look in the spirit of BunsenLabs (Openbox, tint2 and Conky) */
static const mu_Color col_text = {216, 216, 216, 255};
static const mu_Color col_dim = {140, 140, 140, 255};
static const mu_Color col_border = {17, 17, 17, 255};
static const mu_Color col_window = {43, 43, 43, 255};
static const mu_Color col_title_active = {58, 58, 58, 255};
static const mu_Color col_title_active2 = {49, 49, 49, 255};
static const mu_Color col_title_inactive = {40, 40, 40, 255};
static const mu_Color col_panel = {28, 28, 28, 255};

static int text_width(mu_Font font, const char *s, int len) {
	return font_width(font ? font : font_ui, s, len);
}

static int text_height(mu_Font font) {
	return ((const FontData *)(font ? font : font_ui))->height;
}

static void draw_frame(mu_Context *ctx, mu_Rect rect, int colorid) {
	mu_Color c = ctx->style->colors[colorid];
	switch (colorid) {
	case MU_COLOR_WINDOWBG:
		draw_shadow(ctx, rect, 0, 10, 90, 3);
		mu_draw_rect(ctx, rect, c);
		draw_border(ctx, rect, col_border, 0);
		break;
	case MU_COLOR_TITLEBG:
	case MU_COLOR_PANELBG:
		if (c.a)
			mu_draw_rect(ctx, rect, c);
		break;
	case MU_COLOR_SCROLLBASE:
		mu_draw_rect(ctx, rect, c);
		break;
	case MU_COLOR_SCROLLTHUMB:
		draw_rrect(ctx, rect, c, 2);
		break;
	case MU_COLOR_BASE:
	case MU_COLOR_BASEHOVER:
	case MU_COLOR_BASEFOCUS:
		mu_draw_rect(ctx, rect, c);
		draw_border(ctx, rect, colorid == MU_COLOR_BASEFOCUS ? accent : col_border, 0);
		break;
	default:
		/* Buttons: a flat face with a dark border and a faint top highlight */
		draw_rrect(ctx, rect, col_border, 2);
		draw_rrect(ctx, mu_rect(rect.x + 1, rect.y + 1, rect.w - 2, rect.h - 2), c, 1);
		mu_draw_rect(ctx, mu_rect(rect.x + 2, rect.y + 1, rect.w - 4, 1), mu_color(255, 255, 255, 14));
		break;
	}
}

void apply_theme(mu_Context *ctx) {
	mu_Style *st = ctx->style;
	st->font = (mu_Font)font_ui;
	st->size = mu_vec2(68, 14);
	st->padding = 6;
	st->spacing = 5;
	st->indent = 16;
	st->title_height = 16;   /* size of the resize grip */
	st->scrollbar_size = 10;
	st->thumb_size = 8;
	mu_Color *c = st->colors;
	c[MU_COLOR_TEXT] = col_text;
	c[MU_COLOR_BORDER] = mu_color(0, 0, 0, 0);
	c[MU_COLOR_WINDOWBG] = col_window;
	c[MU_COLOR_TITLEBG] = mu_color(0, 0, 0, 0);
	c[MU_COLOR_TITLETEXT] = col_text;
	c[MU_COLOR_PANELBG] = mu_color(0, 0, 0, 0);
	c[MU_COLOR_BUTTON] = mu_color(60, 60, 60, 255);
	c[MU_COLOR_BUTTONHOVER] = mu_color(72, 72, 72, 255);
	c[MU_COLOR_BUTTONFOCUS] = color_mix(mu_color(60, 60, 60, 255), accent, 170);
	c[MU_COLOR_BASE] = mu_color(30, 30, 30, 255);
	c[MU_COLOR_BASEHOVER] = mu_color(34, 34, 34, 255);
	c[MU_COLOR_BASEFOCUS] = mu_color(26, 26, 26, 255);
	c[MU_COLOR_SCROLLBASE] = mu_color(36, 36, 36, 255);
	c[MU_COLOR_SCROLLTHUMB] = mu_color(80, 80, 80, 255);
}

/* --- Widgets shared by applications --- */

void label_font(mu_Context *ctx, const FontData *f, const char *text, mu_Color c) {
	mu_Rect r = mu_layout_next(ctx);
	int y = r.y + (r.h - f->height) / 2;
	mu_draw_text(ctx, (mu_Font)f, text, -1, mu_vec2(r.x, y), c);
}

void widget_progress(mu_Context *ctx, float value, mu_Color color) {
	mu_Rect r = mu_layout_next(ctx);
	r.y += r.h / 2 - 5;
	r.h = 10;
	mu_draw_rect(ctx, r, mu_color(26, 26, 26, 255));
	draw_border(ctx, r, col_border, 0);
	if (value > 1)
		value = 1;
	if (value > 0)
		mu_draw_rect(ctx, mu_rect(r.x + 1, r.y + 1, (int)((r.w - 2) * value), r.h - 2), color);
}

int widget_tile(mu_Context *ctx, const char *label, int kind, mu_Color color) {
	(void)kind;
	(void)color;
	return mu_button(ctx, label);
}

/* --- Windows --- */

#define TITLE_H 26

enum { ICON_MINIMIZE = MU_ICON_MAX + 1, ICON_MAXIMIZE, ICON_RESTORE, ICON_GRIP };

Window *open_app(int app, const char *arg) {
	Window *win = NULL;
	for (int i = 0; i < MAX_WINDOWS; i++) {
		if (!windows[i].used) {
			win = &windows[i];
			break;
		}
	}
	if (!win)
		return NULL;
	memset(win, 0, sizeof(*win));
	win->app = app;
	/* Titles identify windows in microui, so they must be unique */
	int n = 1;
	for (;;) {
		if (n == 1)
			snprintf(win->title, sizeof(win->title), "%s", apps[app].name);
		else
			snprintf(win->title, sizeof(win->title), "%s %d", apps[app].name, n);
		int taken = 0;
		for (int i = 0; i < MAX_WINDOWS; i++)
			if (windows[i].used && !strcmp(windows[i].title, win->title))
				taken = 1;
		if (!taken)
			break;
		n++;
	}
	int count = 0;
	for (int i = 0; i < MAX_WINDOWS; i++)
		count += windows[i].used;
	int w = apps[app].w, h = apps[app].h;
	int x = 60 + (count % 8) * 30;
	int y = PANEL_H + 30 + (count % 8) * 30;
	if (y + h > scr.h - 10)
		y = PANEL_H + 10;
	win->initial = mu_rect(x, y, w, h);
	win->state = apps[app].create ? apps[app].create(win, arg) : NULL;
	win->used = 1;
	win->fresh = 1;
	return win;
}

Window *desk_windows(int *count) {
	*count = MAX_WINDOWS;
	return windows;
}

static void close_window(Window *win) {
	if (apps[win->app].destroy)
		apps[win->app].destroy(win);
	free(win->state);
	win->state = NULL;
	win->used = 0;
	if (active_window == win)
		active_window = NULL;
}

static mu_Container *window_container(Window *win) {
	mu_Container *cnt = mu_get_container(ctx, win->title);
	/* A previous window may have had the same title: reset its container */
	if (win->fresh) {
		win->fresh = 0;
		cnt->open = 1;
		cnt->rect = win->initial;
		mu_bring_to_front(ctx, cnt);
	}
	return cnt;
}

/* Focus a window, restoring it if minimized */
static void raise_window(Window *win) {
	win->minimized = 0;
	mu_bring_to_front(ctx, window_container(win));
	active_window = win;
}

static void toggle_maximize(Window *win, mu_Container *cnt) {
	if (win->maximized) {
		cnt->rect = win->restore;
		win->maximized = 0;
	} else {
		win->restore = cnt->rect;
		cnt->rect = mu_rect(0, PANEL_H, scr.w, scr.h - PANEL_H);
		win->maximized = 1;
	}
}

/* A title bar button. Returns 1 when clicked */
static int title_button(const char *name, int icon, mu_Rect r, int danger) {
	mu_Id id = mu_get_id(ctx, name, strlen(name));
	mu_update_control(ctx, id, r, 0);
	if (ctx->hover == id)
		draw_rrect(ctx, r, danger ? mu_color(176, 58, 58, 255) : mu_color(255, 255, 255, 26), 2);
	mu_draw_icon(ctx, icon, r, ctx->hover == id ? mu_color(240, 240, 240, 255) : col_dim);
	return ctx->mouse_pressed == MU_MOUSE_LEFT && ctx->focus == id;
}

/* Openbox-like decorations: the title on the left, the buttons on the right */
static void decorations(Window *win, mu_Container *cnt, int active) {
	mu_Rect r = mu_rect(cnt->rect.x, cnt->rect.y, cnt->rect.w, TITLE_H);
	if (active)
		draw_gradient(ctx, r, col_title_active, col_title_active2, 0);
	else
		mu_draw_rect(ctx, r, col_title_inactive);
	mu_draw_rect(ctx, mu_rect(r.x, r.y + r.h - 1, r.w, 1), col_border);
	if (active)
		mu_draw_rect(ctx, mu_rect(r.x + 1, r.y + 1, r.w - 2, 1), mu_color(255, 255, 255, 18));
	draw_border(ctx, cnt->rect, col_border, 0);

	int bs = 20, by = r.y + (TITLE_H - bs) / 2;
	mu_Rect close = mu_rect(r.x + r.w - bs - 5, by, bs, bs);
	mu_Rect max = mu_rect(close.x - bs - 2, by, bs, bs);
	mu_Rect min = mu_rect(max.x - bs - 2, by, bs, bs);
	/* Dragging the title moves the window, double clicking maximizes it */
	mu_Rect drag = mu_rect(r.x, r.y, min.x - r.x - 4, TITLE_H);
	mu_Id id = mu_get_id(ctx, "!title", 6);
	mu_update_control(ctx, id, drag, MU_OPT_HOLDFOCUS);
	if (ctx->focus == id && ctx->mouse_pressed == MU_MOUSE_LEFT) {
		long t = now_ms();
		if (t - win->last_click < 400) {
			toggle_maximize(win, cnt);
			win->last_click = 0;
		} else {
			win->last_click = t;
		}
	}
	if (ctx->focus == id && ctx->mouse_down == MU_MOUSE_LEFT && (ctx->mouse_delta.x || ctx->mouse_delta.y)) {
		if (win->maximized) {
			/* Dragging a maximized window restores it under the pointer */
			win->maximized = 0;
			cnt->rect.w = win->restore.w;
			cnt->rect.h = win->restore.h;
			cnt->rect.x = mouse_pos.x - cnt->rect.w / 2;
		}
		cnt->rect.x += ctx->mouse_delta.x;
		cnt->rect.y += ctx->mouse_delta.y;
	}
	draw_appicon(ctx, mu_rect(r.x + 7, r.y + (TITLE_H - 16) / 2, 16, 16), win->app, apps[win->app].color);
	draw_text_font(ctx, font_bold, win->title, r.x + 30, r.y + (TITLE_H - font_bold->height) / 2,
		active ? mu_color(235, 235, 235, 255) : col_dim);
	if (title_button("!min", ICON_MINIMIZE, min, 0)) {
		win->minimized = 1;
		if (active_window == win)
			active_window = NULL;
	}
	if (title_button("!max", win->maximized ? ICON_RESTORE : ICON_MAXIMIZE, max, 0))
		toggle_maximize(win, cnt);
	if (title_button("!close", MU_ICON_CLOSE, close, 1))
		cnt->open = 0;
	/* Resize grip in the bottom right corner (handled by microui) */
	if (!win->maximized)
		mu_draw_icon(ctx, ICON_GRIP, mu_rect(cnt->rect.x + cnt->rect.w - 14, cnt->rect.y + cnt->rect.h - 14, 12, 12),
			mu_color(255, 255, 255, 40));
	/* The content starts below the title bar */
	mu_layout_row(ctx, 1, (int[]){-1}, TITLE_H - ctx->style->padding - ctx->style->spacing + 2);
	mu_layout_next(ctx);
}

/* --- Panel (tint2) and menu (jgmenu) --- */

static mu_Vec2 menu_pos;
static int menu_frame;

static void open_menu(int x, int y) {
	menu_open = 1;
	menu_pos = mu_vec2(x, y);
	/* The click that opened the menu must not close it */
	menu_frame = frame_counter;
}

static void panel(void) {
	int opt = MU_OPT_NOTITLE | MU_OPT_NORESIZE | MU_OPT_NOSCROLL | MU_OPT_NOFRAME | MU_OPT_NOCLOSE;
	if (!mu_begin_window_ex(ctx, "##panel", mu_rect(0, 0, scr.w, PANEL_H), opt))
		return;
	mu_Container *cnt = mu_get_current_container(ctx);
	cnt->rect = mu_rect(0, 0, scr.w, PANEL_H);
	mu_bring_to_front(ctx, cnt);
	mu_draw_rect(ctx, cnt->rect, col_panel);
	mu_draw_rect(ctx, mu_rect(0, PANEL_H - 1, scr.w, 1), mu_color(8, 8, 8, 255));
	mu_draw_rect(ctx, mu_rect(0, 0, scr.w, 1), mu_color(255, 255, 255, 10));
	mu_layout_row(ctx, 1, (int[]){1}, 1);

	/* Menu button */
	mu_Rect r = mu_rect(4, 3, 82, PANEL_H - 6);
	mu_Id id = mu_get_id(ctx, "##menu", 6);
	mu_update_control(ctx, id, r, 0);
	if (ctx->mouse_pressed == MU_MOUSE_LEFT && ctx->focus == id) {
		if (menu_open)
			menu_open = 0;
		else
			open_menu(4, PANEL_H);
	}
	if (menu_open || ctx->hover == id)
		draw_rrect(ctx, r, menu_open ? color_alpha(accent, 255) : mu_color(255, 255, 255, 20), 2);
	draw_appicon(ctx, mu_rect(r.x + 6, r.y + (r.h - 16) / 2, 16, 16), -1, accent);
	draw_text_font(ctx, font_bold, "Menu", r.x + 28, r.y + (r.h - font_bold->height) / 2, col_text);
	mu_draw_rect(ctx, mu_rect(r.x + r.w + 5, 6, 1, PANEL_H - 12), mu_color(255, 255, 255, 18));

	/* Task list: fixed width buttons, shrinking when there are many windows */
	int count = 0;
	for (int i = 0; i < MAX_WINDOWS; i++)
		count += windows[i].used;
	int area = scr.w - 300 - (r.x + r.w + 12);
	int tw = count ? area / count - 4 : 0;
	if (tw > 180)
		tw = 180;
	int x = r.x + r.w + 12;
	for (int i = 0; i < MAX_WINDOWS; i++) {
		Window *win = &windows[i];
		if (!win->used)
			continue;
		mu_Rect tr = mu_rect(x, 3, tw, PANEL_H - 6);
		mu_Id tid = mu_get_id(ctx, win->title, strlen(win->title));
		mu_update_control(ctx, tid, tr, 0);
		if (ctx->mouse_pressed == MU_MOUSE_LEFT && ctx->focus == tid) {
			if (win == active_window && !win->minimized) {
				win->minimized = 1;
				active_window = NULL;
			} else {
				raise_window(win);
			}
		}
		int active = win == active_window && !win->minimized;
		if (active) {
			draw_rrect(ctx, tr, mu_color(255, 255, 255, 28), 2);
			mu_draw_rect(ctx, mu_rect(tr.x + 2, tr.y + tr.h - 2, tr.w - 4, 2), accent);
		} else if (ctx->hover == tid) {
			draw_rrect(ctx, tr, mu_color(255, 255, 255, 14), 2);
		}
		draw_appicon(ctx, mu_rect(tr.x + 6, tr.y + (tr.h - 16) / 2, 16, 16), win->app, apps[win->app].color);
		/* Truncate the title to fit */
		char title[64];
		snprintf(title, sizeof(title), "%s", win->title);
		int maxw = tr.w - 32;
		while (strlen(title) > 1 && font_width(font_ui, title, -1) > maxw) {
			int len = strlen(title);
			title[len - 1] = 0;
			if (len > 4)
				memcpy(title + len - 4, "...", 4);
		}
		mu_Color tc = win->minimized ? col_dim : col_text;
		mu_push_clip_rect(ctx, tr);
		draw_text_font(ctx, font_ui, title, tr.x + 27, tr.y + (tr.h - font_ui->height) / 2, tc);
		mu_pop_clip_rect(ctx);
		x += tw + 4;
	}

	/* System tray: memory and clock */
	char buf[256], mem[64] = "", clk[64];
	if (read_file("/proc/meminfo", buf, sizeof(buf)) > 0) {
		long total = 0, free_kb = 0;
		char *p = strstr(buf, "MemTotal:");
		if (p) total = strtol(p + 9, NULL, 10);
		p = strstr(buf, "MemFree:");
		if (p) free_kb = strtol(p + 8, NULL, 10);
		snprintf(mem, sizeof(mem), "RAM %ldM", (total - free_kb) / 1024);
	}
	time_t t = time(NULL);
	struct tm tm;
	localtime_r(&t, &tm);
	strftime(clk, sizeof(clk), clock_24h ? "%a %d %b  %H:%M" : "%a %d %b  %I:%M %p", &tm);
	int cw = font_width(font_bold, clk, -1);
	int cx = scr.w - cw - 12;
	draw_text_font(ctx, font_bold, clk, cx, (PANEL_H - font_bold->height) / 2, col_text);
	mu_draw_rect(ctx, mu_rect(cx - 12, 6, 1, PANEL_H - 12), mu_color(255, 255, 255, 18));
	int mw = font_width(font_ui, mem, -1);
	int mx = cx - 24 - mw;
	draw_text_font(ctx, font_ui, mem, mx, (PANEL_H - font_ui->height) / 2, col_dim);
	/* Battery: a gauge with the charge, and a mark while charging */
	int level, charging;
	if (battery_state(&level, &charging, NULL, 0)) {
		char pct[16];
		snprintf(pct, sizeof(pct), "%d%%", level);
		int pw = font_width(font_ui, pct, -1);
		int bx = mx - 24 - pw - 26, by = (PANEL_H - 12) / 2;
		mu_Color fg = level <= 10 && !charging ? mu_color(220, 90, 80, 255) : col_text;
		mu_Color edge = mu_color(200, 200, 200, 220);
		mu_draw_rect(ctx, mu_rect(bx, by, 20, 1), edge);
		mu_draw_rect(ctx, mu_rect(bx, by + 11, 20, 1), edge);
		mu_draw_rect(ctx, mu_rect(bx, by, 1, 12), edge);
		mu_draw_rect(ctx, mu_rect(bx + 19, by, 1, 12), edge);
		mu_draw_rect(ctx, mu_rect(bx + 20, by + 3, 2, 6), mu_color(200, 200, 200, 220));
		mu_draw_rect(ctx, mu_rect(bx + 2, by + 2, 16 * level / 100, 8), charging ? color_alpha(accent, 255) : fg);
		draw_text_font(ctx, font_ui, pct, bx + 26, (PANEL_H - font_ui->height) / 2, charging ? col_text : col_dim);
	}
	mu_end_window(ctx);
}

/* Reads the state of the first battery. Returns 0 if there is none */
int battery_state(int *level, int *charging, char *desc, int desc_size) {
	static long last;
	static int cached = -1, cached_charging, has;
	static char cached_status[24];
	long now = now_ms();
	/* The kernel updates it every 10 seconds */
	if (cached < 0 || now - last >= 5000) {
		char buf[32];
		last = now;
		has = 0;
		if (read_file("/sys/class/power_supply/BAT0/present", buf, sizeof(buf)) > 0 && buf[0] == '1'
			&& read_file("/sys/class/power_supply/BAT0/capacity", buf, sizeof(buf)) > 0) {
			cached = atoi(buf);
			has = 1;
			cached_status[0] = 0;
			if (read_file("/sys/class/power_supply/BAT0/status", cached_status, sizeof(cached_status)) > 0)
				cached_status[strcspn(cached_status, "\n")] = 0;
			cached_charging = !strcmp(cached_status, "Charging");
		}
	}
	if (!has)
		return 0;
	*level = cached < 0 ? 0 : cached > 100 ? 100 : cached;
	*charging = cached_charging;
	if (desc)
		snprintf(desc, desc_size, "%d%% %s", *level, cached_status);
	return 1;
}

/* Entries of the menu. APP_COUNT is a separator, -1 restarts the desktop, -2 reboots and -3 powers
 * off (init does it, on SIGTERM and SIGUSR2), -4 starts the web browser */
static const int menu_items[] = {-4, APP_WIFI, APP_TERMINAL, APP_FILES, APP_COUNT, APP_MONITOR, APP_CALC, APP_MINES,
	APP_COUNT, APP_SETTINGS, APP_ABOUT, APP_COUNT, -1, -2, -3};
static const char *menu_actions[] = {"Restart desktop", "Reboot", "Shut down", "Web Browser"};
static void run_fullscreen(const char *path);
static void input_init(void);
static void input_shutdown(void);
static void read_mouse_discard(void);
static const char *menu_labels[APP_COUNT] = {
	[APP_TERMINAL] = "Terminal", [APP_FILES] = "File Manager", [APP_MONITOR] = "System Monitor",
	[APP_CALC] = "Calculator", [APP_MINES] = "Minesweeper", [APP_SETTINGS] = "Preferences",
	[APP_ABOUT] = "About S12 Linux", [APP_WIFI] = "Wi-Fi",
};

/* A globe, for the web browser */
static void draw_globe(mu_Context *ctx, mu_Rect r, mu_Color c) {
	int cx = r.x + r.w / 2, cy = r.y + r.h / 2, rad = r.w / 2 - 1;
	for (int y = -rad; y <= rad; y++)
		for (int x = -rad; x <= rad; x++) {
			int d = x * x + y * y;
			int edge = d <= rad * rad && d >= (rad - 1) * (rad - 1);
			int lines = d < rad * rad && (y == 0 || x == 0 || abs(y) == rad / 2
				|| abs(x * x * 4 + y * y) == rad * rad);
			if (edge || lines)
				mu_draw_rect(ctx, mu_rect(cx + x, cy + y, 1, 1), c);
		}
}

/* Describes the network connection, from /run/net/status (written by netd): "eth0 10.0.0.5" or
 * "Wi-Fi MyNetwork". A connected card wins over one connecting. Cached for 3 seconds */
static int net_state(char *out, int size) {
	static char cached[64];
	static int have;
	static time_t last;
	time_t now = time(NULL);
	if (now - last >= 3 || now < last) {
		last = now;
		char buf[2048];
		have = 0;
		if (read_file("/run/net/status", buf, sizeof(buf)) > 0) {
			have = 1;
			int rank = 0;
			snprintf(cached, sizeof(cached), "not connected");
			for (char *line = strtok(buf, "\n"); line; line = strtok(NULL, "\n")) {
				char name[16] = "";
				sscanf(line, "%15s", name);
				char *inet = strstr(line, " inet ");
				char *wifi = strstr(line, " wifi ");
				if (strstr(line, " active") && inet) {
					char addr[32] = "";
					sscanf(inet + 6, "%31[0-9.]", addr);
					if (wifi)
						snprintf(cached, sizeof(cached), "Wi-Fi %.40s", wifi + 6);
					else
						snprintf(cached, sizeof(cached), "%s %s", name, addr);
					/* Ethernet first, as for the default route */
					if (!wifi)
						break;
					rank = 3;
				} else if (rank < 2 && strstr(line, "link up")) {
					snprintf(cached, sizeof(cached), "connecting...");
					rank = 2;
				} else if (rank < 1 && strstr(line, " no-firmware")) {
					snprintf(cached, sizeof(cached), "Wi-Fi: firmware needed");
					rank = 1;
				}
			}
		}
	}
	if (!have)
		return 0;
	snprintf(out, size, "%s", cached);
	return 1;
}

/* Runs a program that takes the whole screen, such as the browser, and waits for it */
static void run_fullscreen(const char *path) {
	input_shutdown();
	pid_t pid = fork();
	if (pid == 0) {
		execl(path, path, (char *)NULL);
		_exit(127);
	}
	if (pid > 0) {
		while (waitpid(pid, NULL, 0) < 0 && errno == EINTR)
			;
	}
	input_init();
	/* Input made for the program is not for the desktop */
	read_mouse_discard();
	force_redraw();
}

static void menu(void) {
	if (!menu_open)
		return;
	int n = sizeof(menu_items) / sizeof(*menu_items);
	int item_h = 26, sep_h = 9, w = 220;
	int h = 8 + 30;
	for (int i = 0; i < n; i++)
		h += menu_items[i] == APP_COUNT ? sep_h : item_h;
	int x = mu_clamp(menu_pos.x, 0, scr.w - w), y = mu_clamp(menu_pos.y, PANEL_H, scr.h - h);
	int opt = MU_OPT_NOTITLE | MU_OPT_NORESIZE | MU_OPT_NOSCROLL | MU_OPT_NOCLOSE;
	if (!mu_begin_window_ex(ctx, "##menu", mu_rect(x, y, w, h), opt))
		return;
	mu_Container *cnt = mu_get_current_container(ctx);
	cnt->rect = mu_rect(x, y, w, h);
	mu_bring_to_front(ctx, cnt);
	mu_draw_rect(ctx, mu_rect(x + 1, y + 1, w - 2, 30), mu_color(34, 34, 34, 255));
	draw_appicon(ctx, mu_rect(x + 10, y + 8, 16, 16), -1, accent);
	draw_text_font(ctx, font_bold, "S12 Linux", x + 34, y + 4 + (26 - font_bold->height) / 2, col_text);
	int iy = y + 34;
	for (int i = 0; i < n; i++) {
		int app = menu_items[i];
		if (app == APP_COUNT) {
			mu_draw_rect(ctx, mu_rect(x + 8, iy + sep_h / 2, w - 16, 1), mu_color(255, 255, 255, 20));
			iy += sep_h;
			continue;
		}
		mu_Rect r = mu_rect(x + 4, iy, w - 8, item_h);
		mu_Id id = mu_get_id(ctx, &menu_items[i], sizeof(int));
		mu_update_control(ctx, id, r, 0);
		int hover = ctx->hover == id;
		if (hover)
			mu_draw_rect(ctx, r, accent);
		const char *label = app < 0 ? menu_actions[-app - 1] : menu_labels[app];
		if (app >= 0)
			draw_appicon(ctx, mu_rect(r.x + 6, r.y + (item_h - 16) / 2, 16, 16), app, apps[app].color);
		else if (app == -4)
			draw_globe(ctx, mu_rect(r.x + 6, r.y + (item_h - 16) / 2, 16, 16), accent);
		draw_text_font(ctx, font_ui, label, r.x + 30, r.y + (item_h - font_ui->height) / 2,
			hover ? mu_color(255, 255, 255, 255) : col_text);
		if (ctx->mouse_pressed == MU_MOUSE_LEFT && ctx->focus == id) {
			menu_open = 0;
			if (app == -1)
				exit(0);
			if (app == -4) {
				run_fullscreen("/usr/bin/browser");
				continue;
			}
			if (app < 0) {
				kill(1, app == -2 ? SIGTERM : SIGUSR2);
				continue;
			}
			Window *win = open_app(app, NULL);
			if (win)
				active_window = win;
		}
		iy += item_h;
	}
	/* Clicking outside closes the menu */
	if (ctx->mouse_pressed && ctx->hover_root != cnt && frame_counter != menu_frame
		&& !(mouse_pos.y < PANEL_H && mouse_pos.x < 90))
		menu_open = 0;
	mu_end_window(ctx);
}

/* --- Desktop: Conky-like system information --- */

static mu_Container *conky_cnt;

static void conky_line(int x, int *y, const char *label, const char *value) {
	draw_text_font(ctx, font_mono, label, x, *y, mu_color(200, 200, 200, 200));
	draw_text_font(ctx, font_mono, value, x + 100, *y, mu_color(235, 235, 235, 230));
	*y += font_mono->height + 3;
}

static void conky_heading(int x, int *y, int w, const char *title) {
	*y += 6;
	draw_text_font(ctx, font_bold, title, x, *y, color_alpha(accent, 255));
	*y += font_bold->height + 3;
	mu_draw_rect(ctx, mu_rect(x, *y, w, 1), mu_color(255, 255, 255, 50));
	*y += 7;
}

static void conky(void) {
	int w = 270, x = scr.w - w - 30, y0 = PANEL_H + 24;
	int opt = MU_OPT_NOTITLE | MU_OPT_NORESIZE | MU_OPT_NOSCROLL | MU_OPT_NOFRAME | MU_OPT_NOCLOSE;
	if (!mu_begin_window_ex(ctx, "##conky", mu_rect(x, y0, w, 400), opt))
		return;
	conky_cnt = mu_get_current_container(ctx);
	conky_cnt->rect = mu_rect(x, y0, w, 400);
	/* Always below the windows */
	conky_cnt->zindex = -1;
	char buf[512], value[96];
	int y = y0;
	conky_heading(x, &y, w, "S Y S T E M    I N F O");
	struct utsname u;
	uname(&u);
	conky_line(x, &y, "Host", u.nodename[0] ? u.nodename : "s12");
	snprintf(value, sizeof(value), "%s %s", u.sysname, u.release);
	conky_line(x, &y, "Kernel", value);
	conky_line(x, &y, "Machine", u.machine);
	/* Processor load, sampled every second */
	static long prev_busy, prev_idle, last_sample;
	static int load = -1;
	long now = now_ms();
	if (now - last_sample >= 1000 && read_file("/proc/stat", buf, sizeof(buf)) > 0) {
		long user, nice, sys, idle;
		if (sscanf(buf, "cpu %ld %ld %ld %ld", &user, &nice, &sys, &idle) == 4) {
			long busy = user + nice + sys;
			long db = busy - prev_busy, di = idle - prev_idle;
			if (last_sample && db + di > 0)
				load = (int)(100 * db / (db + di));
			prev_busy = busy;
			prev_idle = idle;
		}
		last_sample = now;
	}
	if (load >= 0) {
		snprintf(value, sizeof(value), "%d%%", load);
		conky_line(x, &y, "CPU", value);
		mu_Rect bar = mu_rect(x + 100, y, w - 100, 6);
		mu_draw_rect(ctx, bar, mu_color(255, 255, 255, 30));
		mu_draw_rect(ctx, mu_rect(bar.x, bar.y, bar.w * load / 100, bar.h), color_alpha(accent, 230));
		y += 12;
	}
	if (read_file("/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq", buf, sizeof(buf)) > 0) {
		snprintf(value, sizeof(value), "%ld MHz", strtol(buf, NULL, 10) / 1000);
		conky_line(x, &y, "Frequency", value);
	}
	{
		int level, charging;
		if (battery_state(&level, &charging, value, sizeof(value))) {
			conky_line(x, &y, "Battery", value);
			mu_Rect bar = mu_rect(x + 100, y, w - 100, 6);
			mu_draw_rect(ctx, bar, mu_color(255, 255, 255, 30));
			mu_draw_rect(ctx, mu_rect(bar.x, bar.y, bar.w * level / 100, bar.h), color_alpha(accent, 230));
			y += 12;
		}
	}
	if (net_state(value, sizeof(value)))
		conky_line(x, &y, "Network", value);
	if (read_file("/sys/class/thermal/thermal_zone0/temp", buf, sizeof(buf)) > 0) {
		snprintf(value, sizeof(value), "%ld C", strtol(buf, NULL, 10) / 1000);
		conky_line(x, &y, "Temperature", value);
	}
	if (read_file("/proc/uptime", buf, sizeof(buf)) > 0) {
		long s = strtol(buf, NULL, 10);
		snprintf(value, sizeof(value), "%ldh %02ldm %02lds", s / 3600, s / 60 % 60, s % 60);
		conky_line(x, &y, "Uptime", value);
	}
	if (read_file("/proc/meminfo", buf, sizeof(buf)) > 0) {
		char *p = strstr(buf, "MemTotal:");
		long total = p ? strtol(p + 9, NULL, 10) : 0;
		p = strstr(buf, "MemFree:");
		long free_kb = p ? strtol(p + 8, NULL, 10) : 0;
		snprintf(value, sizeof(value), "%ld / %ld MB", (total - free_kb) / 1024, total / 1024);
		conky_line(x, &y, "RAM", value);
		/* Usage bar */
		mu_Rect bar = mu_rect(x + 100, y, w - 100, 6);
		mu_draw_rect(ctx, bar, mu_color(255, 255, 255, 30));
		if (total)
			mu_draw_rect(ctx, mu_rect(bar.x, bar.y, (int)((long long)bar.w * (total - free_kb) / total), bar.h),
				color_alpha(accent, 230));
		y += 12;
	}
	int procs = 0;
	DIR *d = opendir("/proc");
	struct dirent *de;
	while (d && (de = readdir(d)))
		procs += atoi(de->d_name) > 0;
	if (d)
		closedir(d);
	snprintf(value, sizeof(value), "%d", procs);
	conky_line(x, &y, "Processes", value);
	conky_heading(x, &y, w, "S H O R T C U T S");
	conky_line(x, &y, "Right click", "Root menu");
	conky_line(x, &y, "Menu button", "Applications");
	conky_line(x, &y, "Title bar", "Move, double click");
	conky_line(x, &y, "", "to maximize");
	conky_line(x, &y, "Corner grip", "Resize");
	mu_end_window(ctx);
}

static void frame(void) {
	mu_begin(ctx);
	conky();
	panel();
	/* Right click on the desktop opens the root menu */
	if (ctx->mouse_pressed == MU_MOUSE_RIGHT && (!ctx->hover_root || ctx->hover_root == conky_cnt)
		&& mouse_pos.y > PANEL_H)
		open_menu(mouse_pos.x, mouse_pos.y);
	/* Find the window in front, which gets the keyboard */
	Window *top = NULL;
	int top_z = -1;
	for (int i = 0; i < MAX_WINDOWS; i++) {
		Window *win = &windows[i];
		if (!win->used || win->minimized)
			continue;
		mu_Container *cnt = window_container(win);
		/* Clicking a window focuses it */
		if (ctx->mouse_pressed && ctx->hover_root == cnt)
			active_window = win;
		if (cnt->zindex > top_z) {
			top_z = cnt->zindex;
			top = win;
		}
	}
	if (active_window && (!active_window->used || active_window->minimized))
		active_window = NULL;
	if (!active_window)
		active_window = top;

	for (int i = 0; i < MAX_WINDOWS; i++) {
		Window *win = &windows[i];
		if (!win->used || win->minimized)
			continue;
		int opt = MU_OPT_NOTITLE | MU_OPT_NOSCROLL | (win->maximized ? MU_OPT_NORESIZE : 0);
		if (!mu_begin_window_ex(ctx, win->title, win->initial, opt))
			continue;
		mu_Container *cnt = mu_get_current_container(ctx);
		decorations(win, cnt, win == active_window);
		/* The content is in a panel, which scrolls under the title bar */
		mu_layout_row(ctx, 1, (int[]){-1}, -1);
		mu_begin_panel_ex(ctx, "##body", MU_OPT_NOFRAME);
		apps[win->app].draw(ctx, win);
		mu_end_panel(ctx);
		mu_end_window(ctx);
	}
	menu();
	mu_end(ctx);

	/* Windows closed with their close button */
	for (int i = 0; i < MAX_WINDOWS; i++) {
		Window *win = &windows[i];
		if (win->used && !win->minimized && !win->fresh && !window_container(win)->open)
			close_window(win);
	}
	/* Keep windows below the panel and on screen */
	for (int i = 0; i < MAX_WINDOWS; i++) {
		if (!windows[i].used || windows[i].fresh)
			continue;
		mu_Rect *r = &window_container(&windows[i])->rect;
		if (r->w < 200) r->w = 200;
		if (r->h < 120) r->h = 120;
		if (r->y < PANEL_H) r->y = PANEL_H;
		if (r->y > scr.h - 40) r->y = scr.h - 40;
		if (r->x < -r->w + 80) r->x = -r->w + 80;
		if (r->x > scr.w - 80) r->x = scr.w - 80;
		if (r->w > scr.w) r->w = scr.w;
		if (r->h > scr.h - PANEL_H) r->h = scr.h - PANEL_H;
	}
	frame_counter++;
}

/* --- Rendering --- */

static int cells_w, cells_h;
static uint32_t *cells, *prev_cells;

static inline uint32_t hash_bytes(uint32_t h, const void *data, int size) {
	const unsigned char *p = data;
	for (int i = 0; i < size; i++)
		h = (h ^ p[i]) * 16777619u;
	return h;
}

static void hash_area(mu_Rect r, uint32_t h) {
	r = rect_intersect(r, mu_rect(0, 0, scr.w, scr.h));
	if (r.w <= 0 || r.h <= 0)
		return;
	for (int cy = r.y / CELL; cy <= (r.y + r.h - 1) / CELL; cy++)
		for (int cx = r.x / CELL; cx <= (r.x + r.w - 1) / CELL; cx++) {
			uint32_t *c = &cells[cy * cells_w + cx];
			*c = (*c ^ h) * 16777619u;
		}
}

static mu_Rect text_bounds(mu_TextCommand *t) {
	const FontData *f = t->font ? t->font : font_ui;
	return mu_rect(t->pos.x, t->pos.y, font_width(f, t->str, -1), f->height);
}

static mu_Rect command_bounds(mu_Command *cmd) {
	switch (cmd->type) {
	case MU_COMMAND_RECT: return cmd->rect.rect;
	case MU_COMMAND_TEXT: return text_bounds(&cmd->text);
	case MU_COMMAND_ICON: return cmd->icon.rect;
	case CMD_SHADOW: {
		ShapeCommand *s = (ShapeCommand *)cmd;
		return mu_rect(s->rect.x - s->spread, s->rect.y - s->spread + s->dy,
			s->rect.w + s->spread * 2, s->rect.h + s->spread * 2);
	}
	default: return ((ShapeCommand *)cmd)->rect;
	}
}

static void draw_app_icon_shape(ShapeCommand *s) {
	mu_Rect r = s->rect;
	mu_Color c = s->color;
	mu_Color white = mu_color(255, 255, 255, 240);
	if (s->kind == -1) {
		/* Logo: a ring and a dot */
		gfx_rrect(r, c, r.w / 2);
		gfx_rrect(mu_rect(r.x + 2, r.y + 2, r.w - 4, r.h - 4), mu_color(28, 28, 28, 255), r.w / 2 - 2);
		gfx_rrect(mu_rect(r.x + r.w / 2 - 2, r.y + r.h / 2 - 2, 4, 4), white, 2);
		return;
	}
	int rad = r.w * 18 / 100;
	gfx_vgradient(r, color_mix(c, mu_color(255, 255, 255, 255), 40), c, rad);
	float x = r.x, y = r.y, w = r.w, lw = w / 14.0f > 1.2f ? w / 14.0f : 1.2f;
	switch (s->kind) {
	case APP_TERMINAL:
		gfx_rrect(mu_rect(r.x + r.w / 6, r.y + r.h / 5, r.w * 2 / 3, r.h * 3 / 5), mu_color(10, 12, 18, 200),
			r.w / 10);
		gfx_line(x + w * 0.32f, y + w * 0.38f, x + w * 0.45f, y + w * 0.5f, lw, white);
		gfx_line(x + w * 0.45f, y + w * 0.5f, x + w * 0.32f, y + w * 0.62f, lw, white);
		gfx_line(x + w * 0.52f, y + w * 0.63f, x + w * 0.68f, y + w * 0.63f, lw, white);
		break;
	case APP_FILES:
		gfx_rrect(mu_rect(r.x + r.w * 18 / 100, r.y + r.h * 28 / 100, r.w * 30 / 100, r.h * 16 / 100), white,
			r.w / 20);
		gfx_rrect(mu_rect(r.x + r.w * 18 / 100, r.y + r.h * 36 / 100, r.w * 64 / 100, r.h * 40 / 100), white,
			r.w / 14);
		break;
	case APP_MONITOR:
		for (int i = 0; i < 3; i++) {
			int bh = r.h * (25 + i * 15) / 100;
			gfx_rrect(mu_rect(r.x + r.w * (24 + i * 19) / 100, r.y + r.h * 75 / 100 - bh, r.w * 13 / 100, bh),
				white, r.w / 30);
		}
		break;
	case APP_CALC:
		for (int i = 0; i < 4; i++)
			gfx_rrect(mu_rect(r.x + r.w * (24 + (i % 2) * 28) / 100, r.y + r.h * (24 + (i / 2) * 28) / 100,
				r.w * 24 / 100, r.h * 24 / 100), white, r.w / 16);
		break;
	case APP_MINES:
		gfx_rrect(mu_rect(r.x + r.w * 30 / 100, r.y + r.h * 32 / 100, r.w * 40 / 100, r.h * 40 / 100), white,
			r.w / 5);
		gfx_line(x + w * 0.62f, y + w * 0.34f, x + w * 0.74f, y + w * 0.22f, lw, white);
		gfx_rrect(mu_rect(r.x + r.w * 72 / 100, r.y + r.h * 16 / 100, r.w / 9, r.h / 9), mu_color(255, 220, 120, 255),
			r.w / 18);
		break;
	case APP_SETTINGS:
		for (int i = 0; i < 3; i++) {
			float ly = y + w * (0.32f + i * 0.18f);
			gfx_line(x + w * 0.24f, ly, x + w * 0.76f, ly, lw, color_alpha(white, 160));
			float kx = x + w * (0.36f + ((i * 37) % 30) / 100.0f);
			gfx_rrect(mu_rect((int)kx - r.w / 12, (int)ly - r.w / 12, r.w / 6, r.w / 6), white, r.w / 12);
		}
		break;
	case APP_ABOUT:
		gfx_rrect(mu_rect(r.x + r.w * 44 / 100, r.y + r.h * 22 / 100, r.w * 12 / 100, r.h * 12 / 100), white,
			r.w / 16);
		gfx_rrect(mu_rect(r.x + r.w * 44 / 100, r.y + r.h * 40 / 100, r.w * 12 / 100, r.h * 38 / 100), white,
			r.w / 20);
		break;
	case APP_VIEWER:
		gfx_rrect(mu_rect(r.x + r.w * 26 / 100, r.y + r.h * 18 / 100, r.w * 48 / 100, r.h * 64 / 100), white,
			r.w / 14);
		for (int i = 0; i < 4; i++)
			gfx_rrect(mu_rect(r.x + r.w * 33 / 100, r.y + r.h * (30 + i * 12) / 100, r.w * (34 - (i == 3) * 12) / 100,
				r.h / 20 + 1), color_alpha(c, 220), 1);
		break;
	case APP_WIFI:
		/* A dot under three arcs */
		for (int k = 1; k <= 3; k++) {
			float rad = w * (0.12f + k * 0.13f), cx = x + w * 0.5f, cy = y + w * 0.74f;
			for (int s = 0; s < 8; s++) {
				float a0 = 3.92699f + s * 0.19635f, a1 = a0 + 0.19635f;
				gfx_line(cx + rad * cosf(a0), cy + rad * sinf(a0), cx + rad * cosf(a1), cy + rad * sinf(a1), lw, white);
			}
		}
		gfx_rrect(mu_rect(r.x + r.w * 44 / 100, r.y + r.h * 68 / 100, r.w * 12 / 100, r.h * 12 / 100), white, r.w / 16);
		break;
	}
}

static void draw_commands(mu_Rect damage) {
	mu_Command *cmd = NULL;
	gfx_set_clip(damage);
	while (mu_next_command(ctx, &cmd)) {
		switch (cmd->type) {
		case MU_COMMAND_CLIP:
			gfx_set_clip(rect_intersect(cmd->clip.rect, damage));
			break;
		case MU_COMMAND_RECT:
			gfx_fill(cmd->rect.rect, cmd->rect.color);
			break;
		case MU_COMMAND_TEXT:
			gfx_text(cmd->text.font ? cmd->text.font : font_ui, cmd->text.str, -1, cmd->text.pos.x,
				cmd->text.pos.y, cmd->text.color);
			break;
		case MU_COMMAND_ICON:
			gfx_icon(cmd->icon.id, cmd->icon.rect, cmd->icon.color);
			break;
		case CMD_RRECT: {
			ShapeCommand *s = (ShapeCommand *)cmd;
			gfx_rrect(s->rect, s->color, s->radius);
			break;
		}
		case CMD_BORDER: {
			ShapeCommand *s = (ShapeCommand *)cmd;
			gfx_rrect_border(s->rect, s->color, s->radius);
			break;
		}
		case CMD_SHADOW: {
			ShapeCommand *s = (ShapeCommand *)cmd;
			gfx_shadow(s->rect, s->radius, s->spread, s->alpha, s->dy);
			break;
		}
		case CMD_GRADIENT: {
			ShapeCommand *s = (ShapeCommand *)cmd;
			gfx_vgradient(s->rect, s->color, s->color2, s->radius);
			break;
		}
		case CMD_APPICON:
			draw_app_icon_shape((ShapeCommand *)cmd);
			break;
		}
	}
	gfx_set_clip(damage);
	gfx_cursor(mouse_pos.x, mouse_pos.y);
}

static void render(void) {
	int n = cells_w * cells_h;
	for (int i = 0; i < n; i++)
		cells[i] = 2166136261u;
	/* Hash every command into the cells it covers */
	mu_Rect clip = mu_rect(0, 0, scr.w, scr.h);
	mu_Command *cmd = NULL;
	while (mu_next_command(ctx, &cmd)) {
		if (cmd->type == MU_COMMAND_CLIP) {
			clip = cmd->clip.rect;
			continue;
		}
		mu_Rect b = rect_intersect(command_bounds(cmd), clip);
		uint32_t h = hash_bytes(2166136261u, cmd, cmd->base.size);
		h = hash_bytes(h, &clip, sizeof(clip));
		hash_area(b, h);
	}
	uint32_t ch = hash_bytes(2166136261u, &mouse_pos, sizeof(mouse_pos));
	hash_area(mu_rect(mouse_pos.x, mouse_pos.y, 14, 22), ch);

	/* Redraw the changed cells, merged into horizontal runs */
	for (int cy = 0; cy < cells_h; cy++) {
		int cx = 0;
		while (cx < cells_w) {
			int i = cy * cells_w + cx;
			if (!full_redraw && cells[i] == prev_cells[i]) {
				cx++;
				continue;
			}
			int start = cx;
			while (cx < cells_w && (full_redraw || cells[cy * cells_w + cx] != prev_cells[cy * cells_w + cx]))
				cx++;
			mu_Rect damage = rect_intersect(mu_rect(start * CELL, cy * CELL, (cx - start) * CELL, CELL),
				mu_rect(0, 0, scr.w, scr.h));
			gfx_set_clip(damage);
			gfx_wallpaper(damage, wallpaper_style, accent);
			draw_commands(damage);
			gfx_present(damage);
		}
	}
	memcpy(prev_cells, cells, n * sizeof(*cells));
	full_redraw = 0;
}

/* --- Input --- */

static int mouse_fd = -1, kbd_fd = -1;
static struct termios kbd_saved;

static void input_init(void) {
	mouse_fd = open("/dev/input/mice", O_RDWR | O_NONBLOCK);
	kbd_fd = open("/dev/tty", O_RDWR | O_NONBLOCK);
	/* Without a controlling terminal, the keyboard is read from the first console */
	if (kbd_fd < 0)
		kbd_fd = open("/dev/tty1", O_RDWR | O_NONBLOCK | O_NOCTTY);
	if (kbd_fd >= 0 && tcgetattr(kbd_fd, &kbd_saved) == 0) {
		struct termios raw = kbd_saved;
		raw.c_lflag &= ~(ICANON | ECHO | ISIG | IEXTEN);
		raw.c_iflag &= ~(ICRNL | IXON);
		raw.c_cc[VMIN] = 0;
		raw.c_cc[VTIME] = 0;
		tcsetattr(kbd_fd, TCSANOW, &raw);
	}
	mouse_pos = mu_vec2(scr.w / 2, scr.h / 2);
}

static void input_shutdown(void) {
	if (kbd_fd >= 0)
		tcsetattr(kbd_fd, TCSANOW, &kbd_saved);
}

/* Drops pending mouse packets, and forgets pressed buttons */
static void read_mouse_discard(void) {
	unsigned char p[64];
	while (mouse_fd >= 0 && read(mouse_fd, p, sizeof(p)) > 0)
		;
	if (mouse_buttons & 1)
		mu_input_mouseup(ctx, mouse_pos.x, mouse_pos.y, MU_MOUSE_LEFT);
	mouse_buttons = 0;
}

static int read_mouse(void) {
	unsigned char p[3];
	int changed = 0;
	while (mouse_fd >= 0 && read(mouse_fd, p, 3) == 3) {
		if (getenv("DESK_DEBUG"))
			fprintf(stderr, "mouse %02x %02x %02x\n", p[0], p[1], p[2]);
		if (!(p[0] & 0x08))
			continue;
		int dx = p[1] - ((p[0] << 4) & 0x100);
		int dy = p[2] - ((p[0] << 3) & 0x100);
		/* Simple acceleration */
		int speed = abs(dx) + abs(dy);
		if (speed > 24) {
			dx *= 2;
			dy *= 2;
		} else if (speed > 10) {
			dx = dx * 3 / 2;
			dy = dy * 3 / 2;
		}
		mouse_pos.x = mu_clamp(mouse_pos.x + dx, 0, scr.w - 1);
		mouse_pos.y = mu_clamp(mouse_pos.y - dy, 0, scr.h - 1);
		mu_input_mousemove(ctx, mouse_pos.x, mouse_pos.y);
		static const int map[3] = {MU_MOUSE_LEFT, MU_MOUSE_RIGHT, MU_MOUSE_MIDDLE};
		for (int b = 0; b < 3; b++) {
			int down = p[0] & (1 << b);
			if (down && !(mouse_buttons & (1 << b)))
				mu_input_mousedown(ctx, mouse_pos.x, mouse_pos.y, map[b]);
			else if (!down && (mouse_buttons & (1 << b)))
				mu_input_mouseup(ctx, mouse_pos.x, mouse_pos.y, map[b]);
		}
		mouse_buttons = p[0] & 7;
		changed = 1;
	}
	return changed;
}

/* Gives keyboard input to microui, which only uses text, backspace and return */
static void keys_to_microui(const char *s, int len) {
	for (int i = 0; i < len; i++) {
		unsigned char ch = s[i];
		if (ch == 27) {
			/* Escape sequences (arrows, function keys) are not used by microui */
			if (i + 1 < len && (s[i + 1] == '[' || s[i + 1] == 'O')) {
				i += 2;
				while (i < len && !((s[i] >= 'A' && s[i] <= 'Z') || (s[i] >= 'a' && s[i] <= 'z') || s[i] == '~'))
					i++;
			} else if (menu_open) {
				menu_open = 0;
			}
			continue;
		}
		if (ch == 127 || ch == 8) {
			mu_input_keydown(ctx, MU_KEY_BACKSPACE);
			pending_keyup |= MU_KEY_BACKSPACE;
		} else if (ch == '\r' || ch == '\n') {
			mu_input_keydown(ctx, MU_KEY_RETURN);
			pending_keyup |= MU_KEY_RETURN;
		} else if (ch >= 32) {
			char t[2] = {ch, 0};
			mu_input_text(ctx, t);
		}
	}
}

static int read_keyboard(void) {
	char buf[256];
	int n = kbd_fd >= 0 ? read(kbd_fd, buf, sizeof(buf)) : 0;
	if (n <= 0)
		return 0;
	if (!menu_open && active_window && apps[active_window->app].key
		&& apps[active_window->app].key(active_window, buf, n))
		return 1;
	keys_to_microui(buf, n);
	return 1;
}

/* --- Main loop --- */

static void on_signal(int sig) {
	(void)sig;
	input_shutdown();
	gfx_shutdown();
	_exit(0);
}

static void demo(void) {
	/* Windows opened at start in headless mode, to check the rendering */
	open_app(APP_ABOUT, NULL)->initial = mu_rect(30, 60, 460, 420);
	open_app(APP_MONITOR, NULL)->initial = mu_rect(800, 60, 450, 500);
	open_app(APP_FILES, NULL)->initial = mu_rect(60, 400, 480, 380);
	open_app(APP_TERMINAL, NULL)->initial = mu_rect(420, 300, 660, 400);
}

static void save_screenshot(const char *path) {
	FILE *f = fopen(path, "wb");
	if (!f)
		return;
	fprintf(f, "P6\n%d %d\n255\n", scr.w, scr.h);
	for (int i = 0; i < scr.w * scr.h; i++) {
		uint32_t p = scr.pixels[i];
		unsigned char rgb[3] = {p >> 16, p >> 8, p};
		fwrite(rgb, 1, 3, f);
	}
	fclose(f);
}

int main(int argc, char **argv) {
	(void)argc;
	(void)argv;
	if (gfx_init("/dev/fb0") < 0)
		return 1;
	signal(SIGTERM, on_signal);
	signal(SIGINT, on_signal);
	signal(SIGPIPE, SIG_IGN);
	ctx = calloc(1, sizeof(mu_Context));
	mu_init(ctx);
	ctx->text_width = text_width;
	ctx->text_height = text_height;
	ctx->draw_frame = draw_frame;
	apply_theme(ctx);
	cells_w = (scr.w + CELL - 1) / CELL;
	cells_h = (scr.h + CELL - 1) / CELL;
	cells = calloc(cells_w * cells_h, sizeof(*cells));
	prev_cells = calloc(cells_w * cells_h, sizeof(*cells));

	const char *shot = getenv("DESK_SHOT");
	if (getenv("DESK_HEADLESS")) {
		mouse_pos = mu_vec2(scr.w - 300, scr.h - 200);
		const char *d = getenv("DESK_DEMO");
		if (d && *d == '1')
			demo();
		if (d && *d == '2') {
			open_app(APP_CALC, NULL)->initial = mu_rect(60, 70, 300, 470);
			open_app(APP_MINES, NULL)->initial = mu_rect(400, 90, 400, 500);
			open_app(APP_SETTINGS, NULL)->initial = mu_rect(200, 420, 400, 330);
			open_menu(860, 380);
			mouse_pos = mu_vec2(900, 470);
		}
		/* Scripted input, one event per frame: "m X Y", "d" (press), "u" (release), "t TEXT" */
		const char *ev = getenv("DESK_EVENTS");
		while (ev && *ev) {
			char line[128];
			int n = strcspn(ev, ";");
			snprintf(line, sizeof(line), "%.*s", n, ev);
			ev += n + (ev[n] == ';');
			int x, y;
			if (sscanf(line, " m %d %d", &x, &y) == 2) {
				mouse_pos = mu_vec2(x, y);
				mu_input_mousemove(ctx, x, y);
			} else if (strchr(line, 'R')) {
				mu_input_mousedown(ctx, mouse_pos.x, mouse_pos.y, MU_MOUSE_RIGHT);
			} else if (strchr(line, 'U')) {
				mu_input_mouseup(ctx, mouse_pos.x, mouse_pos.y, MU_MOUSE_RIGHT);
			} else if (strchr(line, 'd')) {
				mu_input_mousedown(ctx, mouse_pos.x, mouse_pos.y, MU_MOUSE_LEFT);
			} else if (strchr(line, 'u')) {
				mu_input_mouseup(ctx, mouse_pos.x, mouse_pos.y, MU_MOUSE_LEFT);
			} else if (line[0] == 't' || line[1] == 't') {
				char *t = strchr(line, 't') + 2;
				if (active_window && apps[active_window->app].key)
					apps[active_window->app].key(active_window, t, strlen(t));
				else
					keys_to_microui(t, strlen(t));
			}
			frame();
			for (int i = 0; i < MAX_WINDOWS; i++)
				if (windows[i].used && apps[windows[i].app].poll)
					apps[windows[i].app].poll(&windows[i]);
			usleep(20000);
		}
		long end = now_ms() + 600;
		while (now_ms() < end) {
			for (int i = 0; i < MAX_WINDOWS; i++)
				if (windows[i].used && apps[windows[i].app].poll)
					apps[windows[i].app].poll(&windows[i]);
			frame();
			usleep(20000);
		}
		frame();
		full_redraw = 1;
		render();
		if (shot)
			save_screenshot(shot);
		return 0;
	}

	input_init();
	if (getenv("DESK_DEBUG"))
		fprintf(stderr, "desk: started, %dx%d\n", scr.w, scr.h);
	if (getenv("DESK_TERMINAL"))
		open_app(APP_TERMINAL, NULL);
	long last_second = 0;
	long last_input = now_ms();
	int blanked = 0;
	for (;;) {
		if (!blanked) {
			frame();
			render();
		}
		if (pending_keyup) {
			/* Keys are released on the frame after they are pressed */
			for (int k = MU_KEY_BACKSPACE; k <= MU_KEY_RETURN; k <<= 1)
				if (pending_keyup & k)
					mu_input_keyup(ctx, k);
			pending_keyup = 0;
			continue;
		}
		/* Wait for input, program output, or the next clock tick */
		long until = 1000 - now_ms() % 1000;
		for (;;) {
			fd_set fds;
			FD_ZERO(&fds);
			int maxfd = -1;
			if (mouse_fd >= 0) { FD_SET(mouse_fd, &fds); maxfd = mouse_fd; }
			if (kbd_fd >= 0) { FD_SET(kbd_fd, &fds); if (kbd_fd > maxfd) maxfd = kbd_fd; }
			extern int app_fds(fd_set *fds, int maxfd);
			maxfd = app_fds(&fds, maxfd);
			struct timeval tv = {until / 1000, (until % 1000) * 1000};
			int r = select(maxfd + 1, &fds, NULL, NULL, &tv);
			int changed = 0;
			if (r > 0) {
				int input = read_mouse();
				input |= read_keyboard();
				if (input) {
					last_input = now_ms();
					changed = 1;
					/* Any input turns the screen back on */
					if (blanked) {
						blanked = 0;
						gfx_blank(0);
						force_redraw();
					}
				}
			}
			if (!blanked && blank_minutes > 0 && now_ms() - last_input >= blank_minutes * 60000L) {
				blanked = 1;
				gfx_blank(1);
			}
			for (int i = 0; i < MAX_WINDOWS; i++)
				if (windows[i].used && apps[windows[i].app].poll)
					changed |= apps[windows[i].app].poll(&windows[i]);
			long second = now_ms() / 1000;
			if (second != last_second) {
				last_second = second;
				changed = 1;
			}
			if (changed || r < 0)
				break;
			until = 1000 - now_ms() % 1000;
		}
	}
}

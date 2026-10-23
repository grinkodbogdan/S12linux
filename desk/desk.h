/* S12 Desk: a desktop built with microui, drawn directly on the framebuffer. */
#ifndef DESK_H
#define DESK_H

#include "gfx.h"
#include "microui.h"

/* Commands added to microui's command list, drawn by our renderer */
enum {
	CMD_RRECT = MU_COMMAND_MAX + 1,
	CMD_BORDER,
	CMD_SHADOW,
	CMD_GRADIENT,
	CMD_APPICON,
};

typedef struct {
	mu_BaseCommand base;
	mu_Rect rect;
	mu_Color color, color2;
	int radius;
	int spread, alpha, dy;   /* shadow */
	int kind;                /* app icon */
} ShapeCommand;

void draw_rrect(mu_Context *ctx, mu_Rect r, mu_Color c, int radius);
void draw_border(mu_Context *ctx, mu_Rect r, mu_Color c, int radius);
void draw_gradient(mu_Context *ctx, mu_Rect r, mu_Color top, mu_Color bottom, int radius);
void draw_appicon(mu_Context *ctx, mu_Rect r, int kind, mu_Color color);
void draw_text_font(mu_Context *ctx, const FontData *f, const char *s, int x, int y, mu_Color c);

/* Theme */
extern mu_Color accent;
extern int wallpaper_style;
extern int clock_24h;
/* Minutes without input before the screen is turned off, zero for never */
extern int blank_minutes;
mu_Color color_alpha(mu_Color c, int a);
mu_Color color_mix(mu_Color a, mu_Color b, int t);
void apply_theme(mu_Context *ctx);
void force_redraw(void);

/* Applications. Each open window is an instance of an application */
enum {
	APP_TERMINAL,
	APP_FILES,
	APP_MONITOR,
	APP_CALC,
	APP_MINES,
	APP_SETTINGS,
	APP_ABOUT,
	APP_VIEWER,
	APP_COUNT
};

typedef struct Window Window;
struct Window {
	int app;
	int used;
	int minimized;
	int fresh;       /* opened since the last frame */
	int maximized;
	mu_Rect restore; /* position before maximizing */
	long last_click; /* for double clicks on the title */
	char title[64];
	mu_Rect initial;
	void *state;
};

typedef struct {
	const char *name;
	const char *desc;
	int w, h;
	mu_Color color;
	int in_launcher;
	void *(*create)(Window *win, const char *arg);
	void (*draw)(mu_Context *ctx, Window *win);
	void (*destroy)(Window *win);
	/* Raw keyboard input, for applications that want it. Returns 1 if used */
	int (*key)(Window *win, const char *s, int len);
	/* Called once per loop. Returns 1 if the window must be redrawn */
	int (*poll)(Window *win);
} App;

extern const App apps[APP_COUNT];
Window *open_app(int app, const char *arg);
/* The window receiving keyboard input */
extern Window *active_window;
extern mu_Vec2 mouse_pos;
extern int frame_counter;
long now_ms(void);
/* First battery: charge in percents and whether it charges, with a description */
int battery_state(int *level, int *charging, char *desc, int desc_size);

/* Helpers for applications */
void widget_progress(mu_Context *ctx, float value, mu_Color color);
int widget_tile(mu_Context *ctx, const char *label, int kind, mu_Color color);
void label_font(mu_Context *ctx, const FontData *f, const char *text, mu_Color c);
int read_file(const char *path, char *buf, int size);

#endif

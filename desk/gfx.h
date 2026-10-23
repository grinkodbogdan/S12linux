/* Software renderer: draws into a RAM back buffer, copied to the framebuffer by damaged areas. */
#ifndef GFX_H
#define GFX_H

#include <stdint.h>
#include "microui.h"

typedef struct {
	int off;         /* offset of the bitmap in the font data */
	short w, h;      /* bitmap size */
	short x, y;      /* bitmap position relative to the pen, y from the top of the line */
	short adv;       /* horizontal advance */
} Glyph;

typedef struct {
	const unsigned char *bitmap;
	const Glyph *glyphs;
	int height;      /* line height */
	int ascent;
} FontData;

extern const FontData *font_ui, *font_bold, *font_small, *font_big, *font_mono;

typedef struct {
	int w, h;
	uint32_t *pixels;    /* back buffer, 0x00RRGGBB */
} Screen;

extern Screen scr;

int gfx_init(const char *fb_path);
void gfx_shutdown(void);
/* Copies a rectangle of the back buffer to the screen */
void gfx_present(mu_Rect r);
/* Turns the screen off (with the panel's power, where supported) or back on */
void gfx_blank(int blank);

void gfx_set_clip(mu_Rect r);
mu_Rect gfx_get_clip(void);

void gfx_fill(mu_Rect r, mu_Color c);
void gfx_rrect(mu_Rect r, mu_Color c, int radius);
/* Rounded rectangle outline of width 1 */
void gfx_rrect_border(mu_Rect r, mu_Color c, int radius);
void gfx_shadow(mu_Rect r, int radius, int spread, int alpha, int dy);
void gfx_vgradient(mu_Rect r, mu_Color top, mu_Color bottom, int radius);
int gfx_text(const FontData *f, const char *s, int len, int x, int y, mu_Color c);
int font_width(const FontData *f, const char *s, int len);
void gfx_icon(int id, mu_Rect r, mu_Color c);
void gfx_line(float x0, float y0, float x1, float y1, float w, mu_Color c);
void gfx_wallpaper(mu_Rect r, int style, mu_Color accent);
void gfx_cursor(int x, int y);

mu_Rect rect_intersect(mu_Rect a, mu_Rect b);

#endif

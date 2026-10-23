/* Software renderer. See gfx.h. */
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/kd.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "gfx.h"
#include "fonts.h"

const FontData *font_ui = &ui, *font_bold = &bold, *font_small = &small, *font_big = &big,
	*font_mono = &mono;

Screen scr;

static uint8_t *fb_mem;
static int fb_fd = -1;
static int fb_pitch;
static int fb_rshift = 16, fb_gshift = 8, fb_bshift = 0;
static int tty_fd = -1;
static mu_Rect clip;

mu_Rect rect_intersect(mu_Rect a, mu_Rect b) {
	int x1 = a.x > b.x ? a.x : b.x;
	int y1 = a.y > b.y ? a.y : b.y;
	int x2 = (a.x + a.w) < (b.x + b.w) ? (a.x + a.w) : (b.x + b.w);
	int y2 = (a.y + a.h) < (b.y + b.h) ? (a.y + a.h) : (b.y + b.h);
	if (x2 < x1) x2 = x1;
	if (y2 < y1) y2 = y1;
	return mu_rect(x1, y1, x2 - x1, y2 - y1);
}

int gfx_init(const char *fb_path) {
	const char *headless = getenv("DESK_HEADLESS");
	if (headless) {
		/* Render without a framebuffer, e.g. for screenshots on the build machine */
		if (sscanf(headless, "%dx%d", &scr.w, &scr.h) != 2) {
			scr.w = 1280;
			scr.h = 800;
		}
	} else {
		int fd = open(fb_path, O_RDWR);
		if (fd < 0) {
			perror(fb_path);
			return -1;
		}
		fb_fd = fd;
		struct fb_var_screeninfo var;
		struct fb_fix_screeninfo fix;
		if (ioctl(fd, FBIOGET_VSCREENINFO, &var) < 0 || ioctl(fd, FBIOGET_FSCREENINFO, &fix) < 0) {
			perror("framebuffer info");
			return -1;
		}
		if (var.bits_per_pixel != 32) {
			fprintf(stderr, "desk: only 32 bits per pixel framebuffers are supported\n");
			return -1;
		}
		scr.w = var.xres;
		scr.h = var.yres;
		fb_pitch = fix.line_length;
		fb_rshift = var.red.offset;
		fb_gshift = var.green.offset;
		fb_bshift = var.blue.offset;
		fb_mem = mmap(NULL, (size_t)fb_pitch * scr.h, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
		if (fb_mem == MAP_FAILED) {
			perror("mmap framebuffer");
			return -1;
		}
		/* Stop the text console from drawing over us */
		tty_fd = open("/dev/tty0", O_RDWR);
		if (tty_fd >= 0)
			ioctl(tty_fd, KDSETMODE, KD_GRAPHICS);
	}
	scr.pixels = calloc((size_t)scr.w * scr.h, 4);
	if (!scr.pixels)
		return -1;
	clip = mu_rect(0, 0, scr.w, scr.h);
	return 0;
}

void gfx_shutdown(void) {
	if (tty_fd >= 0)
		ioctl(tty_fd, KDSETMODE, KD_TEXT);
}

#ifndef FBIO_WAITFORVSYNC
#define FBIO_WAITFORVSYNC _IOW('F', 0x20, unsigned int)
#endif
#ifndef FBIOBLANK
#define FBIOBLANK 0x4611
#endif

void gfx_blank(int blank) {
	if (!fb_mem)
		return;
	if (blank) {
		/* Black first, in case the panel cannot be turned off */
		for (int y = 0; y < scr.h; y++)
			memset(fb_mem + (size_t)y * fb_pitch, 0, (size_t)scr.w * 4);
		ioctl(fb_fd, FBIOBLANK, 4);
	} else {
		ioctl(fb_fd, FBIOBLANK, 0);
	}
}

void gfx_present(mu_Rect r) {
	if (!fb_mem)
		return;
	r = rect_intersect(r, mu_rect(0, 0, scr.w, scr.h));
	/* Large updates start at the vertical blank, so that they do not tear */
	static int vsync = 1;
	if (vsync && (long)r.w * r.h > (long)scr.w * scr.h / 4) {
		unsigned int zero = 0;
		if (ioctl(fb_fd, FBIO_WAITFORVSYNC, &zero) < 0)
			vsync = 0;
	}
	int native = fb_rshift == 16 && fb_gshift == 8 && fb_bshift == 0;
	for (int y = r.y; y < r.y + r.h; y++) {
		uint32_t *src = scr.pixels + y * scr.w + r.x;
		uint32_t *dst = (uint32_t *)(fb_mem + y * fb_pitch) + r.x;
		if (native) {
			memcpy(dst, src, r.w * 4);
			continue;
		}
		for (int x = 0; x < r.w; x++) {
			uint32_t p = src[x];
			dst[x] = ((p >> 16 & 0xff) << fb_rshift) | ((p >> 8 & 0xff) << fb_gshift)
				| ((p & 0xff) << fb_bshift);
		}
	}
}

void gfx_set_clip(mu_Rect r) {
	clip = rect_intersect(r, mu_rect(0, 0, scr.w, scr.h));
}

mu_Rect gfx_get_clip(void) {
	return clip;
}

/* Blends color `c` with coverage `a` (0-255) into the pixel `p` */
static inline void blend(uint32_t *p, mu_Color c, int a) {
	a = a * c.a / 255;
	if (a <= 0)
		return;
	if (a >= 255) {
		*p = (uint32_t)c.r << 16 | (uint32_t)c.g << 8 | c.b;
		return;
	}
	uint32_t d = *p;
	int ia = 255 - a;
	int r = (c.r * a + (d >> 16 & 0xff) * ia + 127) / 255;
	int g = (c.g * a + (d >> 8 & 0xff) * ia + 127) / 255;
	int b = (c.b * a + (d & 0xff) * ia + 127) / 255;
	*p = (uint32_t)r << 16 | (uint32_t)g << 8 | b;
}

void gfx_fill(mu_Rect r, mu_Color c) {
	r = rect_intersect(r, clip);
	if (c.a == 0)
		return;
	uint32_t solid = (uint32_t)c.r << 16 | (uint32_t)c.g << 8 | c.b;
	for (int y = r.y; y < r.y + r.h; y++) {
		uint32_t *row = scr.pixels + y * scr.w;
		if (c.a == 255) {
			for (int x = r.x; x < r.x + r.w; x++)
				row[x] = solid;
		} else {
			for (int x = r.x; x < r.x + r.w; x++)
				blend(&row[x], c, 255);
		}
	}
}

/* Coverage (0-255) of the pixel at (px, py) by a rounded rectangle */
static int rrect_coverage(mu_Rect r, int radius, int px, int py) {
	float cx, cy;
	float fx = px + 0.5f, fy = py + 0.5f;
	if (fx < r.x + radius)
		cx = r.x + radius;
	else if (fx > r.x + r.w - radius)
		cx = r.x + r.w - radius;
	else
		return 255;
	if (fy < r.y + radius)
		cy = r.y + radius;
	else if (fy > r.y + r.h - radius)
		cy = r.y + r.h - radius;
	else
		return 255;
	float d = sqrtf((fx - cx) * (fx - cx) + (fy - cy) * (fy - cy));
	float cov = radius - d + 0.5f;
	if (cov <= 0)
		return 0;
	if (cov >= 1)
		return 255;
	return (int)(cov * 255);
}

void gfx_rrect(mu_Rect r, mu_Color c, int radius) {
	if (radius * 2 > r.w)
		radius = r.w / 2;
	if (radius * 2 > r.h)
		radius = r.h / 2;
	if (radius <= 0) {
		gfx_fill(r, c);
		return;
	}
	mu_Rect cr = rect_intersect(r, clip);
	for (int y = cr.y; y < cr.y + cr.h; y++) {
		uint32_t *row = scr.pixels + y * scr.w;
		int corner_row = y < r.y + radius || y >= r.y + r.h - radius;
		if (!corner_row) {
			mu_Rect line = rect_intersect(mu_rect(r.x, y, r.w, 1), clip);
			gfx_fill(line, c);
			continue;
		}
		for (int x = cr.x; x < cr.x + cr.w; x++) {
			int a = rrect_coverage(r, radius, x, y);
			if (a)
				blend(&row[x], c, a);
		}
	}
}

void gfx_rrect_border(mu_Rect r, mu_Color c, int radius) {
	if (radius * 2 > r.w)
		radius = r.w / 2;
	if (radius * 2 > r.h)
		radius = r.h / 2;
	mu_Rect in = mu_rect(r.x + 1, r.y + 1, r.w - 2, r.h - 2);
	mu_Rect cr = rect_intersect(r, clip);
	for (int y = cr.y; y < cr.y + cr.h; y++) {
		uint32_t *row = scr.pixels + y * scr.w;
		int edge_row = y < r.y + radius + 1 || y >= r.y + r.h - radius - 1;
		for (int x = cr.x; x < cr.x + cr.w; x++) {
			if (!edge_row && x > r.x && x < r.x + r.w - 1) {
				x = r.x + r.w - 2;
				continue;
			}
			int a = rrect_coverage(r, radius, x, y) - rrect_coverage(in, radius - 1, x, y);
			if (a > 0)
				blend(&row[x], c, a);
		}
	}
}

/* Distance from (px, py) to a rounded rectangle, 0 inside */
static float rrect_distance(mu_Rect r, int radius, float px, float py) {
	float cx = px < r.x + radius ? r.x + radius : (px > r.x + r.w - radius ? r.x + r.w - radius : px);
	float cy = py < r.y + radius ? r.y + radius : (py > r.y + r.h - radius ? r.y + r.h - radius : py);
	float d = sqrtf((px - cx) * (px - cx) + (py - cy) * (py - cy)) - radius;
	return d > 0 ? d : 0;
}

void gfx_shadow(mu_Rect r, int radius, int spread, int alpha, int dy) {
	r.y += dy;
	mu_Rect outer = mu_rect(r.x - spread, r.y - spread, r.w + spread * 2, r.h + spread * 2);
	mu_Rect cr = rect_intersect(outer, clip);
	mu_Color black = mu_color(0, 0, 0, 255);
	for (int y = cr.y; y < cr.y + cr.h; y++) {
		uint32_t *row = scr.pixels + y * scr.w;
		int inner_row = y >= r.y + radius && y < r.y + r.h - radius;
		for (int x = cr.x; x < cr.x + cr.w; x++) {
			/* The inside is covered by the window itself */
			if (inner_row && x >= r.x && x < r.x + r.w) {
				x = r.x + r.w - 1;
				continue;
			}
			float d = rrect_distance(r, radius, x + 0.5f, y + 0.5f);
			if (d >= spread)
				continue;
			float t = 1.0f - d / spread;
			blend(&row[x], black, (int)(alpha * t * t));
		}
	}
}

void gfx_vgradient(mu_Rect r, mu_Color top, mu_Color bottom, int radius) {
	mu_Rect cr = rect_intersect(r, clip);
	for (int y = cr.y; y < cr.y + cr.h; y++) {
		int t = r.h > 1 ? (y - r.y) * 255 / (r.h - 1) : 0;
		mu_Color c = mu_color(
			top.r + (bottom.r - top.r) * t / 255,
			top.g + (bottom.g - top.g) * t / 255,
			top.b + (bottom.b - top.b) * t / 255,
			top.a + (bottom.a - top.a) * t / 255);
		mu_Rect saved = clip;
		clip = rect_intersect(clip, mu_rect(r.x, y, r.w, 1));
		gfx_rrect(r, c, radius);
		clip = saved;
	}
}

int font_width(const FontData *f, const char *s, int len) {
	int w = 0;
	if (len < 0)
		len = strlen(s);
	for (int i = 0; i < len; i++) {
		unsigned char ch = s[i];
		if (ch < FONT_FIRST || ch > FONT_LAST)
			ch = '?';
		w += f->glyphs[ch - FONT_FIRST].adv;
	}
	return w;
}

int gfx_text(const FontData *f, const char *s, int len, int x, int y, mu_Color c) {
	int x0 = x;
	if (len < 0)
		len = strlen(s);
	for (int i = 0; i < len; i++) {
		unsigned char ch = s[i];
		if (ch < FONT_FIRST || ch > FONT_LAST)
			ch = '?';
		const Glyph *g = &f->glyphs[ch - FONT_FIRST];
		const unsigned char *bm = f->bitmap + g->off;
		int gx = x + g->x, gy = y + g->y;
		mu_Rect gr = rect_intersect(mu_rect(gx, gy, g->w, g->h), clip);
		for (int py = gr.y; py < gr.y + gr.h; py++) {
			uint32_t *row = scr.pixels + py * scr.w;
			const unsigned char *src = bm + (py - gy) * g->w - gx;
			for (int px = gr.x; px < gr.x + gr.w; px++) {
				if (src[px])
					blend(&row[px], c, src[px]);
			}
		}
		x += g->adv;
	}
	return x - x0;
}

/* Draws an anti-aliased line of width `w` */
void gfx_line(float x0, float y0, float x1, float y1, float w, mu_Color c) {
	int minx = (int)floorf(fminf(x0, x1) - w), maxx = (int)ceilf(fmaxf(x0, x1) + w);
	int miny = (int)floorf(fminf(y0, y1) - w), maxy = (int)ceilf(fmaxf(y0, y1) + w);
	mu_Rect cr = rect_intersect(mu_rect(minx, miny, maxx - minx, maxy - miny), clip);
	float dx = x1 - x0, dy = y1 - y0;
	float len2 = dx * dx + dy * dy;
	for (int y = cr.y; y < cr.y + cr.h; y++) {
		for (int x = cr.x; x < cr.x + cr.w; x++) {
			float px = x + 0.5f - x0, py = y + 0.5f - y0;
			float t = len2 > 0 ? (px * dx + py * dy) / len2 : 0;
			t = t < 0 ? 0 : (t > 1 ? 1 : t);
			float ex = px - t * dx, ey = py - t * dy;
			float d = sqrtf(ex * ex + ey * ey);
			float cov = w / 2 - d + 0.5f;
			if (cov > 0)
				blend(&scr.pixels[y * scr.w + x], c, cov >= 1 ? 255 : (int)(cov * 255));
		}
	}
}

/* Outline of a rectangle, 1 pixel wide */
static void box(int x, int y, int w, int h, mu_Color c) {
	gfx_fill(mu_rect(x, y, w, 1), c);
	gfx_fill(mu_rect(x, y + h - 1, w, 1), c);
	gfx_fill(mu_rect(x, y + 1, 1, h - 2), c);
	gfx_fill(mu_rect(x + w - 1, y + 1, 1, h - 2), c);
}

void gfx_icon(int id, mu_Rect r, mu_Color c) {
	float cx = r.x + r.w / 2.0f, cy = r.y + r.h / 2.0f;
	switch (id) {
	case MU_ICON_CLOSE: {
		float s = 4.0f;
		gfx_line(cx - s, cy - s, cx + s, cy + s, 1.5f, c);
		gfx_line(cx - s, cy + s, cx + s, cy - s, 1.5f, c);
		break;
	}
	case MU_ICON_MAX + 1: /* minimize */
		gfx_fill(mu_rect((int)cx - 4, (int)cy + 2, 9, 2), c);
		break;
	case MU_ICON_MAX + 2: /* maximize: a square with a thick top */
		box((int)cx - 4, (int)cy - 4, 9, 9, c);
		gfx_fill(mu_rect((int)cx - 4, (int)cy - 3, 9, 1), c);
		break;
	case MU_ICON_MAX + 3: /* restore: two overlapping squares */
		box((int)cx - 2, (int)cy - 5, 8, 7, c);
		gfx_fill(mu_rect((int)cx - 5, (int)cy - 2, 8, 7), mu_color(52, 52, 52, 255));
		box((int)cx - 5, (int)cy - 2, 8, 7, c);
		break;
	case MU_ICON_MAX + 4: /* resize grip: three diagonal lines */
		for (int i = 0; i < 3; i++) {
			float o = i * 4.0f;
			gfx_line(r.x + r.w - 1, r.y + o + 1, r.x + o + 1, r.y + r.h - 1, 1, c);
		}
		break;
	case MU_ICON_CHECK:
		gfx_line(cx - 5, cy, cx - 1.5f, cy + 3.5f, 2, c);
		gfx_line(cx - 1.5f, cy + 3.5f, cx + 5, cy - 4, 2, c);
		break;
	case MU_ICON_COLLAPSED:
		gfx_line(cx - 2, cy - 4, cx + 2, cy, 1.6f, c);
		gfx_line(cx + 2, cy, cx - 2, cy + 4, 1.6f, c);
		break;
	case MU_ICON_EXPANDED:
		gfx_line(cx - 4, cy - 2, cx, cy + 2, 1.6f, c);
		gfx_line(cx, cy + 2, cx + 4, cy - 2, 1.6f, c);
		break;
	}
}

void gfx_wallpaper(mu_Rect r, int style, mu_Color accent) {
	r = rect_intersect(r, clip);
	/* Dark backgrounds with a soft vignette, and a faint logo ring left of center */
	static const float base[3][3] = {{46, 46, 46}, {44, 50, 58}, {40, 40, 40}};
	const float *b = base[style < 0 || style > 2 ? 0 : style];
	float cx = scr.w * 0.42f, cy = scr.h * 0.52f;
	float maxd = (scr.w * scr.w + scr.h * scr.h) * 0.25f;
	float ring = scr.h * 0.16f;
	for (int y = r.y; y < r.y + r.h; y++) {
		uint32_t *row = scr.pixels + y * scr.w;
		for (int x = r.x; x < r.x + r.w; x++) {
			float dx = x - cx, dy = y - cy;
			float d2 = dx * dx + dy * dy;
			float k = 1.0f;
			if (style != 2)
				k = 1.0f - 0.45f * (d2 / maxd);
			float fr = b[0] * k, fg = b[1] * k, fb = b[2] * k;
			if (style != 2) {
				/* Logo ring, anti-aliased */
				float d = sqrtf(d2);
				float e = fabsf(d - ring) - 7;
				if (e < 1) {
					float a = (e < 0 ? 1 : 1 - e) * 0.10f;
					fr += (accent.r - fr) * a;
					fg += (accent.g - fg) * a;
					fb += (accent.b - fb) * a;
				}
				if (d < 14) {
					float a = (d < 13 ? 1 : 14 - d) * 0.10f;
					fr += (accent.r - fr) * a;
					fg += (accent.g - fg) * a;
					fb += (accent.b - fb) * a;
				}
				int n = ((x * 7 + y * 13) & 3) - 1;
				fr += n * 0.5f;
				fg += n * 0.5f;
				fb += n * 0.5f;
			}
			int ir = fr > 255 ? 255 : (fr < 0 ? 0 : (int)fr);
			int ig = fg > 255 ? 255 : (fg < 0 ? 0 : (int)fg);
			int ib = fb > 255 ? 255 : (fb < 0 ? 0 : (int)fb);
			row[x] = (uint32_t)ir << 16 | (uint32_t)ig << 8 | ib;
		}
	}
}

/* Arrow cursor: '#' outline, '.' fill */
static const char *cursor_img[] = {
	"#",
	"##",
	"#.#",
	"#..#",
	"#...#",
	"#....#",
	"#.....#",
	"#......#",
	"#.......#",
	"#........#",
	"#.........#",
	"#......#####",
	"#...#..#",
	"#..##..#",
	"#.#  #..#",
	"##   #..#",
	"#     #..#",
	"      #..#",
	"       ##",
};

void gfx_cursor(int x, int y) {
	mu_Rect saved = clip;
	mu_Color shade = mu_color(0, 0, 0, 60);
	/* Soft shadow, then the arrow */
	for (int pass = 0; pass < 2; pass++) {
		for (int j = 0; j < (int)(sizeof(cursor_img) / sizeof(*cursor_img)); j++) {
			for (int i = 0; cursor_img[j][i]; i++) {
				char ch = cursor_img[j][i];
				if (ch == ' ')
					continue;
				if (pass == 0) {
					gfx_fill(mu_rect(x + i + 1, y + j + 2, 1, 1), shade);
				} else {
					mu_Color c = ch == '#' ? mu_color(20, 20, 24, 255) : mu_color(255, 255, 255, 255);
					gfx_fill(mu_rect(x + i, y + j, 1, 1), c);
				}
			}
		}
	}
	clip = saved;
}

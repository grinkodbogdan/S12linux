/*
 * Touchpad daemon. The kernel's /dev/input/mice turns a touchpad into a mouse without scrolling
 * (that is done by libinput on other systems). tpd takes the touchpad over and drives a virtual
 * mouse with a wheel (uinput), which /dev/input/mice includes:
 *   - one finger moves the pointer, with acceleration
 *   - a tap clicks; a tap with two fingers is a right click, with three a middle click
 *   - two fingers, or one finger along the right edge, scroll
 *   - the physical buttons are passed through
 */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <math.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#define BITS(n) (((n) + 8 * sizeof(long) - 1) / (8 * sizeof(long)))
#define TEST(bit, array) ((array[(bit) / (8 * sizeof(long))] >> ((bit) % (8 * sizeof(long)))) & 1)

/* Taps: shorter than this, and moving less than TAP_MOVE of the width */
#define TAP_MS 180
#define TAP_MOVE 0.03
/* The right edge, for scrolling with one finger: this part of the width */
#define EDGE 0.07
/* Scrolling: one wheel step for this part of the height */
#define SCROLL_STEP 0.06
/* Pointer speed: the width of the pad moves the pointer this many pixels, before acceleration */
#define PAD_WIDTH_PIXELS 900.0

static int ui = -1;

static void emit(int type, int code, int value) {
	struct input_event ev = {.type = type, .code = code, .value = value};
	if (write(ui, &ev, sizeof(ev)) < 0)
		perror("tpd: write");
}

static void syn(void) {
	emit(EV_SYN, SYN_REPORT, 0);
}

static void click(int button) {
	emit(EV_KEY, button, 1);
	syn();
	emit(EV_KEY, button, 0);
	syn();
}

static long now_ms(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* Tells whether an input device is a touchpad: absolute X and Y, touch, and no pen */
static int is_touchpad(int fd) {
	unsigned long ev[BITS(EV_MAX + 1)] = {0}, abs[BITS(ABS_MAX + 1)] = {0}, key[BITS(KEY_MAX + 1)] = {0};
	if (ioctl(fd, EVIOCGBIT(0, sizeof(ev)), ev) < 0 || !TEST(EV_ABS, ev) || !TEST(EV_KEY, ev))
		return 0;
	ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(abs)), abs);
	ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(key)), key);
	return TEST(ABS_X, abs) && TEST(ABS_Y, abs) && (TEST(BTN_TOUCH, key) || TEST(BTN_TOOL_FINGER, key))
		&& !TEST(BTN_TOOL_PEN, key);
}

static int find_touchpad(char *name, int size) {
	DIR *d = opendir("/dev/input");
	struct dirent *de;
	while (d && (de = readdir(d))) {
		if (strncmp(de->d_name, "event", 5))
			continue;
		char path[64];
		snprintf(path, sizeof(path), "/dev/input/%s", de->d_name);
		int fd = open(path, O_RDONLY | O_NONBLOCK);
		if (fd < 0)
			continue;
		char n[128] = "";
		ioctl(fd, EVIOCGNAME(sizeof(n)), n);
		/* Not our own virtual mouse */
		if (strcmp(n, "S12 touchpad") && is_touchpad(fd)) {
			snprintf(name, size, "%s", n);
			closedir(d);
			return fd;
		}
		close(fd);
	}
	if (d)
		closedir(d);
	return -1;
}

static int create_mouse(void) {
	int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
	if (fd < 0)
		return -1;
	ioctl(fd, UI_SET_EVBIT, EV_KEY);
	ioctl(fd, UI_SET_EVBIT, EV_REL);
	ioctl(fd, UI_SET_KEYBIT, BTN_LEFT);
	ioctl(fd, UI_SET_KEYBIT, BTN_RIGHT);
	ioctl(fd, UI_SET_KEYBIT, BTN_MIDDLE);
	ioctl(fd, UI_SET_RELBIT, REL_X);
	ioctl(fd, UI_SET_RELBIT, REL_Y);
	ioctl(fd, UI_SET_RELBIT, REL_WHEEL);
	struct uinput_setup setup = {0};
	setup.id.bustype = BUS_VIRTUAL;
	setup.id.vendor = 0x5312;
	setup.id.product = 0x0001;
	snprintf(setup.name, sizeof(setup.name), "S12 touchpad");
	if (ioctl(fd, UI_DEV_SETUP, &setup) < 0 || ioctl(fd, UI_DEV_CREATE) < 0) {
		close(fd);
		return -1;
	}
	return fd;
}

static void run(int tp, const char *name);

int main(int argc, char **argv) {
	(void)argc;
	(void)argv;
	/* The touchpad may appear a little after boot. Without one within a minute, there is none */
	char name[128] = "";
	int tp = -1;
	for (int i = 0; i < 60 && tp < 0; i++) {
		tp = find_touchpad(name, sizeof(name));
		if (tp < 0)
			sleep(1);
	}
	if (tp < 0) {
		fprintf(stderr, "tpd: no touchpad\n");
		return 0;
	}
	ui = create_mouse();
	if (ui < 0) {
		perror("tpd: /dev/uinput");
		return 1;
	}
	for (;;) {
		run(tp, name);
		/* The touchpad went away (e.g. a USB one unplugged): wait for one to come back */
		close(tp);
		fprintf(stderr, "tpd: touchpad lost\n");
		while ((tp = find_touchpad(name, sizeof(name))) < 0)
			sleep(2);
	}
}

/* Turns the touchpad's events into the virtual mouse's, until the touchpad goes away */
static void run(int tp, const char *name) {
	/* Only tpd reads the touchpad from now on: /dev/input/mice gets the virtual mouse instead */
	if (ioctl(tp, EVIOCGRAB, 1) < 0)
		perror("tpd: grab");
	struct input_absinfo ax = {0}, ay = {0};
	ioctl(tp, EVIOCGABS(ABS_X), &ax);
	ioctl(tp, EVIOCGABS(ABS_Y), &ay);
	double width = ax.maximum > ax.minimum ? ax.maximum - ax.minimum : 1000;
	double height = ay.maximum > ay.minimum ? ay.maximum - ay.minimum : 1000;
	fprintf(stderr, "tpd: %s, %.0f x %.0f\n", name, width, height);

	int x = 0, y = 0, touching = 0, fingers = 0, have_pos = 0;
	int px = 0, py = 0, prev_fingers = 0, was_touching = 0;
	long touch_start = 0;
	int start_x = 0, start_y = 0, max_fingers = 0, moved = 0, edge = 0;
	double fx = 0, fy = 0, scroll = 0;
	for (;;) {
		struct pollfd p = {.fd = tp, .events = POLLIN};
		if (poll(&p, 1, -1) < 0 && errno != EINTR)
			break;
		struct input_event evs[64];
		ssize_t n = read(tp, evs, sizeof(evs));
		if (n < 0) {
			if (errno == EAGAIN || errno == EINTR)
				continue;
			break;
		}
		for (int i = 0; i < (int)(n / sizeof(evs[0])); i++) {
			struct input_event *e = &evs[i];
			if (e->type == EV_ABS && e->code == ABS_X) {
				x = e->value;
				have_pos = 1;
			} else if (e->type == EV_ABS && e->code == ABS_Y) {
				y = e->value;
				have_pos = 1;
			} else if (e->type == EV_KEY) {
				switch (e->code) {
				case BTN_TOUCH: touching = e->value; break;
				case BTN_TOOL_FINGER: if (e->value) fingers = 1; else if (fingers == 1) fingers = 0; break;
				case BTN_TOOL_DOUBLETAP: if (e->value) fingers = 2; else if (fingers == 2) fingers = 1; break;
				case BTN_TOOL_TRIPLETAP: if (e->value) fingers = 3; else if (fingers == 3) fingers = 2; break;
				case BTN_TOOL_QUADTAP: if (e->value) fingers = 4; else if (fingers == 4) fingers = 3; break;
				case BTN_LEFT:
				case BTN_RIGHT:
				case BTN_MIDDLE:
					emit(EV_KEY, e->code, e->value);
					syn();
					break;
				}
			} else if (e->type == EV_SYN && e->code == SYN_REPORT) {
				if (touching && !was_touching) {
					touch_start = now_ms();
					start_x = x;
					start_y = y;
					max_fingers = fingers > 0 ? fingers : 1;
					moved = 0;
					edge = x > ax.maximum - width * EDGE;
					scroll = 0;
					fx = fy = 0;
				}
				if (touching && fingers > max_fingers)
					max_fingers = fingers;
				if (touching && was_touching && have_pos && fingers == prev_fingers) {
					int dx = x - px, dy = y - py;
					if (abs(x - start_x) > width * TAP_MOVE || abs(y - start_y) > width * TAP_MOVE)
						moved = 1;
					if (fingers >= 2 || edge) {
						/* Scrolling: fingers down scroll the page down (the wheel turned down) */
						scroll += dy;
						double step = height * SCROLL_STEP;
						while (scroll >= step) {
							emit(EV_REL, REL_WHEEL, -1);
							syn();
							scroll -= step;
						}
						while (scroll <= -step) {
							emit(EV_REL, REL_WHEEL, 1);
							syn();
							scroll += step;
						}
					} else if (dx || dy) {
						double k = PAD_WIDTH_PIXELS / width;
						double speed = sqrt((double)dx * dx + (double)dy * dy) * k;
						double accel = speed > 16 ? 2.2 : speed > 8 ? 1.6 : speed > 3 ? 1.2 : 1.0;
						fx += dx * k * accel;
						fy += dy * k * accel;
						int mx = (int)fx, my = (int)fy;
						fx -= mx;
						fy -= my;
						if (mx || my) {
							if (mx)
								emit(EV_REL, REL_X, mx);
							if (my)
								emit(EV_REL, REL_Y, my);
							syn();
						}
					}
				}
				if (!touching && was_touching) {
					/* A tap: short and still */
					if (now_ms() - touch_start < TAP_MS && !moved && !edge)
						click(max_fingers >= 3 ? BTN_MIDDLE : max_fingers == 2 ? BTN_RIGHT : BTN_LEFT);
				}
				px = x;
				py = y;
				prev_fingers = fingers;
				was_touching = touching;
			}
		}
	}
}

# S12 Desk, the init program, the touchpad daemon and the sound test, as static 32-bit x86 programs.
# CC must build static i686 Linux binaries, e.g. "zig cc -target x86-linux-musl -mcpu=bonnell".
# `make host` builds the desktop for the build machine, for testing in a window-less terminal.
SRC = main.c apps.c gfx.c term.c microui.c
CFLAGS = -Os -std=gnu11 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers
CC ?= cc
STRIP ?= strip

all: desk minit soundtest tpd

desk: $(SRC) *.h
	$(CC) $(CFLAGS) -static -o $@ $(SRC) -lm
	$(STRIP) $@

minit: init.c
	$(CC) -Os -std=gnu11 -Wall -Wextra -static -o $@ init.c
	$(STRIP) $@

tpd: tpd.c
	$(CC) -Os -std=gnu11 -Wall -Wextra -static -o $@ tpd.c -lm
	$(STRIP) $@

soundtest: soundtest.c
	$(CC) -Os -std=gnu11 -Wall -static -o $@ soundtest.c -lm
	$(STRIP) $@

host: $(SRC) *.h
	cc $(CFLAGS) -g -o desk-host $(SRC) -lm -lutil

clean:
	rm -f desk desk-host minit soundtest tpd

.PHONY: all host clean

#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <unistd.h>
/* Plays 440 Hz (16 bits stereo, 44.1 kHz) for a second, then 880 Hz (8 bits mono, 22.05 kHz) */
#define SNDCTL_DSP_SPEED 0xc0045002
#define SNDCTL_DSP_SETFMT 0xc0045005
#define SNDCTL_DSP_CHANNELS 0xc0045006
#define SNDCTL_DSP_SYNC 0x5001
#define SOUND_MIXER_WRITE_VOLUME 0xc0044d00
#define SOUND_MIXER_READ_VOLUME 0x80044d00
int main(void) {
	int m = open("/dev/mixer", O_RDWR);
	int v = 90 | (90 << 8);
	if (m < 0 || ioctl(m, SOUND_MIXER_WRITE_VOLUME, &v) < 0) perror("mixer");
	v = 0; ioctl(m, SOUND_MIXER_READ_VOLUME, &v);
	printf("tone: volume %d\n", v & 0xff);
	int fd = open("/dev/dsp", O_WRONLY);
	if (fd < 0) { perror("dsp"); return 1; }
	int fmt = 0x10, ch = 2, rate = 44100;
	ioctl(fd, SNDCTL_DSP_SETFMT, &fmt); ioctl(fd, SNDCTL_DSP_CHANNELS, &ch); ioctl(fd, SNDCTL_DSP_SPEED, &rate);
	printf("tone: fmt %#x ch %d rate %d\n", fmt, ch, rate);
	static short buf[44100 * 2];
	for (int i = 0; i < 44100; i++) buf[2 * i] = buf[2 * i + 1] = (short)(12000 * sin(2 * M_PI * 440 * i / rate));
	long w = write(fd, buf, sizeof(buf));
	ioctl(fd, SNDCTL_DSP_SYNC, 0);
	fmt = 0x08; ch = 1; rate = 22050;
	ioctl(fd, SNDCTL_DSP_SETFMT, &fmt); ioctl(fd, SNDCTL_DSP_CHANNELS, &ch); ioctl(fd, SNDCTL_DSP_SPEED, &rate);
	static unsigned char b8[11025];
	for (int i = 0; i < 11025; i++) b8[i] = (unsigned char)(128 + 60 * sin(2 * M_PI * 880 * i / rate));
	long w2 = write(fd, b8, sizeof(b8));
	ioctl(fd, SNDCTL_DSP_SYNC, 0);
	printf("tone: wrote %ld + %ld bytes, rate2 %d\n", w, w2, rate);
	return 0;
}

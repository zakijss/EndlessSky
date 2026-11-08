// Hardware probe for the Miyoo Mini Plus port: screen layout, button codes, OSS audio.
// Restores the screen and releases the buttons when it exits. Runs from /tmp.
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <linux/soundcard.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <sys/time.h>
#include <unistd.h>

static double Now(void)
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return tv.tv_sec + tv.tv_usec / 1e6;
}

static int fbFd = -1;
static uint8_t *fbMap, *fbSaved;
static size_t fbSize;
static struct fb_var_screeninfo fbVar;

static void ScreenShow(void)
{
	int fd = open("/dev/fb0", O_RDWR);
	if(fd < 0) { printf("fb0: %s\n", strerror(errno)); return; }
	struct fb_var_screeninfo v;
	struct fb_fix_screeninfo f;
	ioctl(fd, FBIOGET_VSCREENINFO, &v);
	ioctl(fd, FBIOGET_FSCREENINFO, &f);
	printf("fb: %ux%u virtual %ux%u offset %u,%u bpp %u stride %u rotate %u\n",
		v.xres, v.yres, v.xres_virtual, v.yres_virtual, v.xoffset, v.yoffset,
		v.bits_per_pixel, f.line_length, v.rotate);
	printf("fb: red %u/%u green %u/%u blue %u/%u alpha %u/%u\n", v.red.offset, v.red.length,
		v.green.offset, v.green.length, v.blue.offset, v.blue.length, v.transp.offset, v.transp.length);
	size_t size = (size_t)f.line_length * v.yres_virtual;
	uint8_t *fb = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if(fb == MAP_FAILED) { printf("mmap: %s\n", strerror(errno)); close(fd); return; }
	uint8_t *saved = malloc(size);
	memcpy(saved, fb, size);

	// Pattern in the visible page: red top-left, green top-right, blue bottom-left,
	// white bottom-right, and a yellow bar along the top 24 rows.
	// Both pages of the double buffer, so a page flip still shows the pattern.
	for(unsigned y = 0; y < v.yres_virtual; ++y)
	{
		uint32_t *row = (uint32_t *)(fb + (size_t)y * f.line_length);
		for(unsigned x = 0; x < v.xres; ++x)
		{
			unsigned py = y % v.yres;
			int right = x >= v.xres / 2, bottom = py >= v.yres / 2;
			uint32_t c = !bottom ? (right ? 0xFF00FF00 : 0xFFFF0000) : (right ? 0xFFFFFFFF : 0xFF0000FF);
			if(py < 24)
				c = 0xFFFFFF00;
			row[x] = c;
		}
	}
	ioctl(fd, FBIOPAN_DISPLAY, &v);
	puts("screen: pattern shown");
	fflush(stdout);
	fbFd = fd; fbMap = fb; fbSaved = saved; fbSize = size; fbVar = v;
}

static void ScreenRestore(void)
{
	if(fbFd < 0)
		return;
	memcpy(fbMap, fbSaved, fbSize);
	ioctl(fbFd, FBIOPAN_DISPLAY, &fbVar);
	free(fbSaved);
	munmap(fbMap, fbSize);
	close(fbFd);
	fbFd = -1;
	puts("screen: restored");
}

static void Buttons(int seconds)
{
	int fd = open("/dev/input/event0", O_RDONLY);
	if(fd < 0) { printf("event0: %s\n", strerror(errno)); return; }
	char name[64] = "";
	ioctl(fd, EVIOCGNAME(sizeof(name)), name);
	int grabbed = ioctl(fd, EVIOCGRAB, 1) == 0;
	printf("buttons: '%s', grabbed %d, logging for %d s\n", name, grabbed, seconds);
	fflush(stdout);
	double end = Now() + seconds, last = 0.;
	while(Now() < end && !(last && Now() - last > 10.))
	{
		fd_set set;
		FD_ZERO(&set);
		FD_SET(fd, &set);
		struct timeval tv = {0, 200000};
		if(select(fd + 1, &set, NULL, NULL, &tv) <= 0)
			continue;
		struct input_event e;
		if(read(fd, &e, sizeof(e)) != sizeof(e))
			break;
		if(e.type == EV_KEY && e.value == 1)
		{
			printf("  key %d down\n", e.code);
			last = Now();
		}
		else if(e.type == EV_ABS)
			printf("  abs %d = %d\n", e.code, e.value);
		fflush(stdout);
	}
	if(grabbed)
		ioctl(fd, EVIOCGRAB, 0);
	close(fd);
	puts("buttons: released");
}

static void Tone(void)
{
	int fd = open("/dev/dsp", O_WRONLY | O_NONBLOCK);
	if(fd < 0) { printf("dsp: %s\n", strerror(errno)); return; }
	fcntl(fd, F_SETFL, 0);
	int fmt = AFMT_S16_LE, ch = 2, rate = 48000;
	int r1 = ioctl(fd, SNDCTL_DSP_SETFMT, &fmt);
	int r2 = ioctl(fd, SNDCTL_DSP_CHANNELS, &ch);
	int r3 = ioctl(fd, SNDCTL_DSP_SPEED, &rate);
	printf("dsp: fmt %d ch %d rate %d (ioctl %d %d %d)\n", fmt, ch, rate, r1, r2, r3);
	fflush(stdout);
	static int16_t buf[48000 * 2];
	for(int i = 0; i < rate && i < 48000; ++i)
	{
		// Quiet 440 Hz with a short fade in and out.
		double env = fmin(1., fmin(i, rate - i) / 2000.);
		int16_t s = (int16_t)(10000. * env * sin(2. * M_PI * 440. * i / rate));
		buf[2 * i] = buf[2 * i + 1] = s;
	}
	double t = Now();
	ssize_t w = write(fd, buf, sizeof(int16_t) * 2 * (rate < 48000 ? rate : 48000));
	printf("dsp: wrote %zd bytes in %.2f s\n", w, Now() - t);
	sleep(1); // let the audio server finish playing before the stream closes
	close(fd);
}

int main(int argc, char **argv)
{
	int seconds = argc > 1 ? atoi(argv[1]) : 60;
	ScreenShow();
	if(seconds > 0)
		Buttons(seconds);
	else
		sleep(5);
	ScreenRestore();
	Tone();
	return 0;
}

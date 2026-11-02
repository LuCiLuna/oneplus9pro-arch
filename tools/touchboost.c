/*
 * touchboost - raise CPU minimum frequencies while the touchscreen is in use.
 *
 * Phosh draws its UI on the CPU (GTK3). schedutil only ramps up after the
 * load has built up, so a short swipe runs on a slow core at a low clock.
 * Like Android's input boost, raise each cluster's scaling_min_freq on touch
 * and restore it HOLD_MS after the last event.
 *
 * usage: touchboost [device-name]   (default "Synaptics TouchComm Touchscreen")
 */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#define HOLD_MS 1000

static const struct { int policy; long boost_khz; } boosts[] = {
	{ 0, 1209600 },	/* A55 */
	{ 4, 1996800 },	/* A78 */
	{ 7, 1785600 },	/* X1 */
};
#define NB (sizeof(boosts) / sizeof(boosts[0]))
static long saved[NB];

static long read_long(const char *path)
{
	FILE *f = fopen(path, "r");
	long v = -1;

	if (f) {
		if (fscanf(f, "%ld", &v) != 1)
			v = -1;
		fclose(f);
	}
	return v;
}

static void write_long(const char *path, long v)
{
	FILE *f = fopen(path, "w");

	if (f) {
		fprintf(f, "%ld\n", v);
		fclose(f);
	}
}

static void set_min(int boost)
{
	char path[128];

	for (size_t i = 0; i < NB; i++) {
		snprintf(path, sizeof(path),
			 "/sys/devices/system/cpu/cpufreq/policy%d/scaling_min_freq",
			 boosts[i].policy);
		if (boost)
			saved[i] = read_long(path);
		if (saved[i] > 0)
			write_long(path, boost && boosts[i].boost_khz > saved[i] ?
					 boosts[i].boost_khz : saved[i]);
	}
}

static int open_dev(const char *want)
{
	DIR *d = opendir("/dev/input");
	struct dirent *e;
	char path[300], name[256];
	int fd = -1;

	if (!d)
		return -1;
	while ((e = readdir(d))) {
		if (strncmp(e->d_name, "event", 5))
			continue;
		snprintf(path, sizeof(path), "/dev/input/%s", e->d_name);
		fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
		if (fd < 0)
			continue;
		if (ioctl(fd, EVIOCGNAME(sizeof(name)), name) >= 0 && !strcmp(name, want))
			break;
		close(fd);
		fd = -1;
	}
	closedir(d);
	return fd;
}

static long now_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

int main(int argc, char **argv)
{
	const char *want = argc > 1 ? argv[1] : "Synaptics TouchComm Touchscreen";
	struct input_event ev[64];
	long until = 0;
	int boosted = 0, fd = -1;

	for (;;) {
		struct pollfd p;
		int timeout, r;

		if (fd < 0) {
			fd = open_dev(want);
			if (fd < 0) {
				sleep(2);
				continue;
			}
		}
		timeout = boosted ? (int)(until - now_ms()) : -1;
		if (boosted && timeout <= 0) {
			set_min(0);
			boosted = 0;
			continue;
		}
		p.fd = fd;
		p.events = POLLIN;
		r = poll(&p, 1, timeout);
		if (r < 0 && errno != EINTR)
			break;
		if (r <= 0)
			continue;
		if (read(fd, ev, sizeof(ev)) < 0 && errno != EAGAIN) {
			close(fd);	/* device went away (driver reload) */
			fd = -1;
			continue;
		}
		until = now_ms() + HOLD_MS;
		if (!boosted) {
			set_min(1);
			boosted = 1;
		}
	}
	if (boosted)
		set_min(0);
	return 0;
}

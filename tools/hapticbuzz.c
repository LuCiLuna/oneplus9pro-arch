/*
 * Plays one rumble effect on a force feedback input device.
 *
 *   hapticbuzz /dev/input/eventN [milliseconds]
 *
 * aarch64-linux-gnu-gcc -static -O2 -o hapticbuzz hapticbuzz.c
 */
#include <fcntl.h>
#include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <unistd.h>

int main(int argc, char **argv)
{
	int ms = argc > 2 ? atoi(argv[2]) : 200;
	struct ff_effect e = { .type = FF_RUMBLE, .id = -1 };
	struct input_event ev = { .type = EV_FF };
	int fd;

	if (argc < 2 || (fd = open(argv[1], O_RDWR)) < 0)
		return perror("open"), 1;

	e.u.rumble.strong_magnitude = 0xffff;
	e.replay.length = ms;
	if (ioctl(fd, EVIOCSFF, &e) < 0)
		return perror("EVIOCSFF"), 1;

	ev.code = e.id;
	ev.value = 1;
	if (write(fd, &ev, sizeof(ev)) != sizeof(ev))
		return perror("play"), 1;

	usleep((ms + 100) * 1000);
	close(fd);
	return 0;
}

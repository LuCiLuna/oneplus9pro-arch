/*
 * Applies the alert slider position to feedbackd: top silent, middle vibrate
 * only, bottom full sound. Runs as a user service.
 *
 * aarch64-linux-gnu-gcc -static -O2 -o alert-slider alert-slider.c
 */
#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>

#define DEVICE_NAME "OnePlus Alert Slider"

static int open_slider(void)
{
	struct dirent *d;
	DIR *dir = opendir("/dev/input");
	int fd = -1;

	if (!dir)
		return -1;

	while (fd < 0 && (d = readdir(dir))) {
		char path[300], name[64] = "";
		int f;

		if (strncmp(d->d_name, "event", 5))
			continue;
		snprintf(path, sizeof(path), "/dev/input/%s", d->d_name);
		f = open(path, O_RDONLY | O_CLOEXEC);
		if (f < 0)
			continue;
		if (ioctl(f, EVIOCGNAME(sizeof(name)), name) > 0 && !strcmp(name, DEVICE_NAME))
			fd = f;
		else
			close(f);
	}
	closedir(dir);
	return fd;
}

static void apply(int position)
{
	static const char *const profiles[] = { "silent", "quiet", "full" };
	pid_t pid;

	if (position < 0 || position > 2)
		return;

	pid = fork();
	if (pid == 0) {
		execlp("gsettings", "gsettings", "set", "org.sigxcpu.feedbackd", "profile",
		       profiles[position], (char *)NULL);
		_exit(127);
	}
	if (pid > 0)
		waitpid(pid, NULL, 0);
}

int main(void)
{
	struct input_absinfo abs;
	struct input_event ev;
	int fd;

	while ((fd = open_slider()) < 0)
		sleep(2);

	if (!ioctl(fd, EVIOCGABS(ABS_MISC), &abs))
		apply(abs.value);

	while (read(fd, &ev, sizeof(ev)) == sizeof(ev))
		if (ev.type == EV_ABS && ev.code == ABS_MISC)
			apply(ev.value);

	return 1;
}

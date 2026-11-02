/* Measure a TLMM pad's toggle rate by polling its GPIO_IN bit through /dev/mem.
 * Reading the IN bit is harmless even while the pad is muxed to another
 * function (e.g. mdp_vsync).  usage: tepoll <gpio> [seconds] */
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <time.h>

#define TLMM_BASE 0x0f100000UL

static double now(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

int main(int argc, char **argv)
{
	int gpio = argc > 1 ? atoi(argv[1]) : 82;
	double secs = argc > 2 ? atof(argv[2]) : 2.0;
	int fd = open("/dev/mem", O_RDONLY | O_SYNC);
	volatile uint32_t *r;
	double t0, t, first = 0, last = 0, hi_start = 0, hi_total = 0, gap_min = 1, gap_max = 0;
	int prev, cur, rises = 0;

	if (fd < 0) { perror("/dev/mem"); return 1; }
	r = mmap(NULL, 0x1000, PROT_READ, MAP_SHARED, fd, TLMM_BASE + gpio * 0x1000UL);
	if (r == MAP_FAILED) { perror("mmap"); return 1; }
	printf("cfg 0x%08x\n", r[0]);
	prev = r[1] & 1;
	t0 = now();
	while ((t = now()) - t0 < secs) {
		cur = r[1] & 1;
		if (cur && !prev) {
			if (rises) {
				double g = t - last;
				if (g < gap_min) gap_min = g;
				if (g > gap_max) gap_max = g;
			} else {
				first = t;
			}
			last = t;
			hi_start = t;
			rises++;
		} else if (!cur && prev && hi_start) {
			hi_total += t - hi_start;
		}
		prev = cur;
	}
	if (rises > 1)
		printf("gpio%d: %d rising edges, %.2f Hz, period %.3f..%.3f ms, avg high %.1f us\n",
		       gpio, rises, (rises - 1) / (last - first), gap_min * 1e3, gap_max * 1e3,
		       hi_total / rises * 1e6);
	else
		printf("gpio%d: %d rising edges\n", gpio, rises);
	return 0;
}

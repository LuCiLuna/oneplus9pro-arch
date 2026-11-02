// SPDX-License-Identifier: GPL-2.0-only
/*
 * regdump: read (or write) 32-bit MMIO registers through /dev/mem.
 *   regdump <phys-addr> [count]       dump count words (default 1)
 *   regdump -w <phys-addr> <value>    write one word
 */
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

int main(int argc, char **argv)
{
	int wr = argc > 1 && !strcmp(argv[1], "-w");
	unsigned long addr, count = 1, page = sysconf(_SC_PAGESIZE);
	uint32_t val = 0;
	volatile uint32_t *p;
	void *map;
	size_t len;
	int fd;

	if (argc < 2 + wr || (wr && argc < 4)) {
		fprintf(stderr, "usage: %s <addr> [count] | -w <addr> <value>\n", argv[0]);
		return 1;
	}
	addr = strtoul(argv[1 + wr], NULL, 0);
	if (wr)
		val = strtoul(argv[3], NULL, 0);
	else if (argc > 2)
		count = strtoul(argv[2], NULL, 0);

	fd = open("/dev/mem", O_RDWR | O_SYNC);
	if (fd < 0) {
		perror("/dev/mem");
		return 1;
	}
	len = (addr & (page - 1)) + count * 4;
	map = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, addr & ~(page - 1));
	if (map == MAP_FAILED) {
		perror("mmap");
		return 1;
	}
	p = (volatile uint32_t *)((char *)map + (addr & (page - 1)));

	if (wr) {
		*p = val;
		printf("%#010lx <- %#010x (now %#010x)\n", addr, val, *p);
	} else {
		for (unsigned long i = 0; i < count; i++)
			printf("%#010lx: %#010x\n", addr + i * 4, p[i]);
	}
	return 0;
}

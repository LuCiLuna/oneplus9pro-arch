// SPDX-License-Identifier: GPL-2.0-only
/*
 * dmcreate: create and activate a device-mapper device from a table on stdin,
 * for initramfs use where dmsetup is unavailable.
 *
 *   dmcreate <name> < table
 *
 * Each table line: "<start> <length> <target-type> <params...>"
 * e.g. "0 77248 linear 259:3 10524672"
 */
#include <fcntl.h>
#include <linux/dm-ioctl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define BUF_SIZE (64 * 1024)
#define MAX_LINE 1024

static void init_hdr(struct dm_ioctl *io, const char *name, size_t size)
{
	memset(io, 0, sizeof(*io));
	io->version[0] = DM_VERSION_MAJOR;
	io->version[1] = DM_VERSION_MINOR;
	io->version[2] = DM_VERSION_PATCHLEVEL;
	io->data_size = size;
	io->data_start = sizeof(*io);
	strncpy(io->name, name, sizeof(io->name) - 1);
}

int main(int argc, char **argv)
{
	static char buf[BUF_SIZE] __attribute__((aligned(8)));
	struct dm_ioctl *io = (struct dm_ioctl *)buf;
	struct dm_target_spec *spec, *prev = NULL;
	char line[MAX_LINE], type[DM_MAX_TYPE_NAME];
	unsigned long long start, len;
	size_t off = sizeof(*io);
	int fd, n, count = 0;

	if (argc != 2) {
		fprintf(stderr, "usage: %s <name> < table\n", argv[0]);
		return 1;
	}
	fd = open("/dev/mapper/control", O_RDWR);
	if (fd < 0) {
		perror("/dev/mapper/control");
		return 1;
	}

	init_hdr(io, argv[1], sizeof(*io));
	if (ioctl(fd, DM_DEV_CREATE, io)) {
		perror("DM_DEV_CREATE");
		return 1;
	}

	init_hdr(io, argv[1], BUF_SIZE);
	while (fgets(line, sizeof(line), stdin)) {
		char *params;
		size_t plen;

		if (sscanf(line, "%llu %llu %31s %n", &start, &len, type, &n) < 3)
			continue;
		params = line + n;
		params[strcspn(params, "\n")] = 0;
		plen = strlen(params) + 1;
		if (off + sizeof(*spec) + plen + 8 > BUF_SIZE) {
			fprintf(stderr, "table too large\n");
			return 1;
		}
		spec = (struct dm_target_spec *)(buf + off);
		memset(spec, 0, sizeof(*spec));
		spec->sector_start = start;
		spec->length = len;
		strncpy(spec->target_type, type, sizeof(spec->target_type) - 1);
		memcpy(spec + 1, params, plen);
		if (prev)
			prev->next = (char *)spec - (char *)prev;
		prev = spec;
		off += sizeof(*spec) + plen;
		off = (off + 7) & ~(size_t)7;
		count++;
	}
	if (!count) {
		fprintf(stderr, "empty table\n");
		return 1;
	}
	io->target_count = count;
	io->data_size = off;
	if (ioctl(fd, DM_TABLE_LOAD, io)) {
		perror("DM_TABLE_LOAD");
		return 1;
	}

	init_hdr(io, argv[1], sizeof(*io));
	if (ioctl(fd, DM_DEV_SUSPEND, io)) {	/* no DM_SUSPEND_FLAG: resume = activate */
		perror("DM_DEV_SUSPEND (resume)");
		return 1;
	}
	printf("/dev/dm-%u\n", (unsigned int)(io->dev & 0xff) | (unsigned int)((io->dev >> 12) & 0xfff00));
	return 0;
}

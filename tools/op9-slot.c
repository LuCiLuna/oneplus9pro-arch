// SPDX-License-Identifier: GPL-2.0-only
/*
 * op9-slot: show or set Qualcomm A/B slot flags on the OnePlus 9 Pro, the way
 * Android's boot_control HAL does: edit only the attribute bits of the
 * boot_<slot> GPT entry, in both primary and backup tables, and fix the CRCs.
 * Does not rewrite or resize anything else (the boot LUN uses 96 entries).
 *
 *   op9-slot show
 *   op9-slot mark-successful <a|b> [--write]
 *
 * Qualcomm ABL attribute bits: 48-49 priority, 50 active, 51-53 retry count,
 * 54 successful, 55 unbootable.
 */
#include <dirent.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <linux/fs.h>
#include <unistd.h>

#define ATTR_SUCCESSFUL	(1ULL << 54)
#define ATTR_UNBOOTABLE	(1ULL << 55)
#define ATTR_RETRY_SHIFT	51
#define ATTR_RETRY_MASK	(7ULL << ATTR_RETRY_SHIFT)

struct gpt_header {
	char sig[8];
	uint32_t revision, header_size, header_crc, reserved;
	uint64_t my_lba, alt_lba, first_usable, last_usable;
	uint8_t disk_guid[16];
	uint64_t entries_lba;
	uint32_t num_entries, entry_size, entries_crc;
} __attribute__((packed));

struct gpt_entry {
	uint8_t type[16], uniq[16];
	uint64_t first, last, attrs;
	uint16_t name[36];
} __attribute__((packed));

static uint32_t crc32(const void *buf, size_t len)
{
	const uint8_t *p = buf;
	uint32_t c = 0xffffffff;

	while (len--) {
		c ^= *p++;
		for (int k = 0; k < 8; k++)
			c = (c >> 1) ^ (0xedb88320 & -(c & 1));
	}
	return ~c;
}

static int name_is(const struct gpt_entry *e, const char *s)
{
	for (int i = 0; i < 36; i++) {
		if ((char)e->name[i] != s[i] || (e->name[i] >> 8))
			return 0;
		if (!s[i])
			return 1;
	}
	return 0;
}

/* Find /dev/sdX that holds a partition named `part` via sysfs. */
static int find_disk(const char *part, char *disk, size_t len)
{
	char path[512], line[256];
	struct dirent *d;
	DIR *dir = opendir("/sys/class/block");

	if (!dir)
		return -1;
	while ((d = readdir(dir))) {
		FILE *f;

		snprintf(path, sizeof(path), "/sys/class/block/%s/uevent", d->d_name);
		f = fopen(path, "r");
		if (!f)
			continue;
		while (fgets(line, sizeof(line), f)) {
			if (!strncmp(line, "PARTNAME=", 9) && !strncmp(line + 9, part, strlen(part)) &&
			    line[9 + strlen(part)] == '\n') {
				/* sde16 -> sde */
				snprintf(disk, len, "/dev/%.*s", (int)strcspn(d->d_name, "0123456789"), d->d_name);
				fclose(f);
				closedir(dir);
				return 0;
			}
		}
		fclose(f);
	}
	closedir(dir);
	return -1;
}

struct table {
	uint64_t hdr_lba;
	struct gpt_header hdr;
	uint8_t *entries;
	size_t entries_len;
};

static int read_table(int fd, uint32_t ss, uint64_t lba, struct table *t)
{
	uint8_t *sec = malloc(ss);

	t->hdr_lba = lba;
	if (pread(fd, sec, ss, lba * ss) != (ssize_t)ss)
		return -1;
	memcpy(&t->hdr, sec, sizeof(t->hdr));
	free(sec);
	if (memcmp(t->hdr.sig, "EFI PART", 8))
		return -1;
	t->entries_len = (size_t)t->hdr.num_entries * t->hdr.entry_size;
	t->entries = malloc(t->entries_len);
	if (pread(fd, t->entries, t->entries_len, t->hdr.entries_lba * ss) != (ssize_t)t->entries_len)
		return -1;
	if (crc32(t->entries, t->entries_len) != t->hdr.entries_crc) {
		fprintf(stderr, "entries CRC mismatch at LBA %llu\n", (unsigned long long)lba);
		return -1;
	}
	return 0;
}

static struct gpt_entry *find_entry(struct table *t, const char *name)
{
	for (uint32_t i = 0; i < t->hdr.num_entries; i++) {
		struct gpt_entry *e = (struct gpt_entry *)(t->entries + (size_t)i * t->hdr.entry_size);

		if (name_is(e, name))
			return e;
	}
	return NULL;
}

static int write_table(int fd, uint32_t ss, struct table *t)
{
	uint8_t *sec = calloc(1, ss);

	t->hdr.entries_crc = crc32(t->entries, t->entries_len);
	if (pread(fd, sec, ss, t->hdr_lba * ss) != (ssize_t)ss)
		return -1;
	memcpy(sec, &t->hdr, sizeof(t->hdr));
	((struct gpt_header *)sec)->header_crc = 0;
	((struct gpt_header *)sec)->header_crc = crc32(sec, t->hdr.header_size);
	if (pwrite(fd, t->entries, t->entries_len, t->hdr.entries_lba * ss) != (ssize_t)t->entries_len ||
	    pwrite(fd, sec, ss, t->hdr_lba * ss) != (ssize_t)ss)
		return -1;
	free(sec);
	return 0;
}

static void show(uint64_t a, const char *name)
{
	printf("%-7s attrs=%#018llx priority=%llu active=%llu retry=%llu successful=%llu unbootable=%llu\n",
	       name, (unsigned long long)a, (unsigned long long)((a >> 48) & 3),
	       (unsigned long long)((a >> 50) & 1), (unsigned long long)((a >> 51) & 7),
	       (unsigned long long)((a >> 54) & 1), (unsigned long long)((a >> 55) & 1));
}

int main(int argc, char **argv)
{
	int mark = argc >= 3 && !strcmp(argv[1], "mark-successful");
	int doit = argc >= 4 && !strcmp(argv[3], "--write");
	char disk[64], target[16];
	struct table pri, bak;
	uint32_t ss = 512;
	int fd;

	if (!(argc == 2 && !strcmp(argv[1], "show")) && !mark) {
		fprintf(stderr, "usage: %s show | mark-successful <a|b> [--write]\n", argv[0]);
		return 1;
	}
	if (find_disk("boot_a", disk, sizeof(disk))) {
		fprintf(stderr, "boot_a not found\n");
		return 1;
	}
	fd = open(disk, doit ? O_RDWR | O_SYNC : O_RDONLY);
	if (fd < 0 || ioctl(fd, BLKSSZGET, &ss)) {
		perror(disk);
		return 1;
	}
	if (read_table(fd, ss, 1, &pri) || read_table(fd, ss, pri.hdr.alt_lba, &bak)) {
		fprintf(stderr, "%s: cannot read GPT\n", disk);
		return 1;
	}
	printf("%s (sector %u, %u entries)\n", disk, ss, pri.hdr.num_entries);
	show(find_entry(&pri, "boot_a")->attrs, "boot_a");
	show(find_entry(&pri, "boot_b")->attrs, "boot_b");
	if (!mark)
		return 0;

	snprintf(target, sizeof(target), "boot_%c", argv[2][0]);
	struct gpt_entry *ep = find_entry(&pri, target), *eb = find_entry(&bak, target);

	if (!ep || !eb || ep->attrs != eb->attrs) {
		fprintf(stderr, "%s missing or primary/backup attributes differ\n", target);
		return 1;
	}
	uint64_t a = ep->attrs;

	a |= ATTR_SUCCESSFUL;
	a &= ~ATTR_UNBOOTABLE;
	a = (a & ~ATTR_RETRY_MASK) | (7ULL << ATTR_RETRY_SHIFT);
	if (a == ep->attrs) {
		printf("%s already marked successful\n", target);
		return 0;
	}
	show(a, "new");
	if (!doit) {
		printf("dry run (add --write to apply)\n");
		return 0;
	}
	ep->attrs = eb->attrs = a;
	if (write_table(fd, ss, &bak) || write_table(fd, ss, &pri)) {
		perror("write");
		return 1;
	}
	fsync(fd);
	ioctl(fd, BLKRRPART);	/* best effort; ignore busy */
	printf("%s marked successful\n", target);
	return 0;
}

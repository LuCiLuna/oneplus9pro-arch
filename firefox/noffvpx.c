/*
 * LD_PRELOAD shim for Firefox on the OnePlus 9 Pro.
 *
 * Firefox's decoder process (RDD) always tries its bundled FFmpeg (FFVPX) first,
 * and that copy has no V4L2 decoders, so VP9 never reaches the hardware. No pref
 * changes the order. Failing the load of the bundled library in RDD makes Firefox
 * fall through to the system FFmpeg module.
 *
 * Only RDD: the Utility process decodes audio, and it needs FFVPX for Opus, MP3,
 * FLAC and Vorbis.
 *
 * RDD is forked from the fork server, so a constructor can't tell which process
 * this is. Firefox rewrites the process title in place though, so keep a pointer
 * to it and read it at load time, which needs no syscalls under the sandbox. If
 * this ever misses, FFVPX stays loaded and video falls back to software decoding.
 *
 * aarch64-linux-gnu-gcc -shared -fPIC -O2 -o libnoffvpx.so noffvpx.c
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stddef.h>
#include <string.h>

static const char *process_title;

__attribute__((constructor))
static void save_process_title(int argc, char **argv, char **envp)
{
	(void)envp;

	if (argc > 0)
		process_title = argv[0];
}

static int is_rdd_process(void)
{
	static const char suffix[] = " rdd";
	size_t len;

	if (!process_title)
		return 0;

	len = strlen(process_title);

	return len >= sizeof(suffix) - 1 &&
	       !strcmp(process_title + len - (sizeof(suffix) - 1), suffix);
}

void *dlopen(const char *filename, int flags)
{
	static void *(*real_dlopen)(const char *, int);

	if (!real_dlopen)
		real_dlopen = dlsym(RTLD_NEXT, "dlopen");

	if (filename && strstr(filename, "libmozavcodec.so") && is_rdd_process())
		return NULL;

	return real_dlopen(filename, flags);
}

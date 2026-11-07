/* Inkluderas med -include i alla host-verktyg på macOS.
 * Darwin definierar uuid_t (unsigned char[16]) via <unistd.h>, vilket krockar
 * med scripts/mod/file2alias.c. Definiera _UUID_T så att Darwins typedef hoppas över. */
#ifndef VROOM_PRELUDE_H
#define VROOM_PRELUDE_H
#define _UUID_T
#define __GETHOSTUUID_H  /* deklarerar gethostuuid(uuid_t, ...), används inte */

/* Linux-specifikt som host-verktyg använder (scripts/gen_init_cpio.c m.fl.) */
#include <sys/types.h>
#include <errno.h>
#include <fcntl.h>
#ifndef O_LARGEFILE
#define O_LARGEFILE 0
#endif
static inline ssize_t copy_file_range(int in, long long *ioff, int out, long long *ooff,
				      size_t len, unsigned int flags)
{
	(void)in; (void)ioff; (void)out; (void)ooff; (void)len; (void)flags;
	errno = ENOSYS;	/* anroparna faller tillbaka på read/write */
	return -1;
}

/* Darwin kallar struct stat-fälten st_*timespec (scripts/basic/depcheck.c) */
#ifdef __APPLE__
#define st_mtim st_mtimespec
#define st_atim st_atimespec
#define st_ctim st_ctimespec
#endif
#endif

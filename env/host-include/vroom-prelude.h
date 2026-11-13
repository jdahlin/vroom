/* Included with -include in all host tools on macOS.
 * Darwin defines uuid_t (unsigned char[16]) via <unistd.h>, which clashes
 * with scripts/mod/file2alias.c. Define _UUID_T so that Darwin's typedef is skipped. */
#ifndef VROOM_PRELUDE_H
#define VROOM_PRELUDE_H
#define _UUID_T
#define __GETHOSTUUID_H  /* declares gethostuuid(uuid_t, ...), unused */

/* Linux-specific things host tools use (scripts/gen_init_cpio.c etc.) */
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
	errno = ENOSYS;	/* callers fall back to read/write */
	return -1;
}

/* Darwin calls the struct stat fields st_*timespec (scripts/basic/depcheck.c) */
#ifdef __APPLE__
#define st_mtim st_mtimespec
#define st_atim st_atimespec
#define st_ctim st_ctimespec
#endif
#endif

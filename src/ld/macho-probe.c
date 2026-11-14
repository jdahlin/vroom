/* The test for a Mach-O first input. */

#include "ld-internal.h"

/* the first input file (an argument that is not an option or its value) is Mach-O */
VPRIV bool ld_first_input_is_macho(int argc, char **argv, int dirfd)
{
	for (int i = 1; i < argc; i++) {
		const char *a = argv[i];
		if (a[0] == '-') {
			if (!strcmp(a, "-o") || !strcmp(a, "-T") || !strcmp(a, "-arch") || !strcmp(a, "-L") ||
			    !strcmp(a, "-syslibroot"))
				i++;
			continue;
		}
		int fd = openat(dirfd, a, O_RDONLY | O_CLOEXEC);
		if (fd < 0)
			return false;
		uint32_t m = 0;
		bool r = read(fd, &m, 4) == 4 && m == 0xfeedfacfu;
		close(fd);
		return r;
	}
	return false;
}

/* The lock that serialises links and the test for a Mach-O first input. */

static pthread_mutex_t ldlock = PTHREAD_MUTEX_INITIALIZER;

/* the first input file (an argument that is not an option or its value) is Mach-O */
static bool first_input_macho(int argc, char **argv, int dirfd)
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

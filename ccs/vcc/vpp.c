/*
 * vpp: the preprocessor alone (see ppdriver.c), for parity tests.
 *
 *   vpp [--cmp REF.i] [-o OUT] CLANG-ARGS...
 *   vpp --cmp-files OURS REF      compare two -E outputs
 *   VPP_WARM=N   N untimed passes first (fills the token cache; profiling)
 */
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "pp.h"
#include "ppdriver.h"

int main(int argc, char **argv)
{
	ppd_env e = { NULL, 1, 2 };
	const char *w = getenv("VPP_WARM");

	if (argc == 4 && !strcmp(argv[1], "--cmp-files")) {
		int fd = open(argv[2], O_RDONLY);
		struct stat st;

		if (fd < 0 || fstat(fd, &st))
			return 2;
		return ppd_compare(mmap(NULL, (size_t)st.st_size + 1, PROT_READ, MAP_PRIVATE, fd, 0),
				   (size_t)st.st_size, argv[3]);
	}

	for (int r = w ? atoi(w) : 0; r > 0; r--) {
		ppd_env q = { NULL, -1, 2 };
		char *av[argc + 3];
		int n = 0;

		for (int i = 0; i < argc; i++)
			av[n++] = argv[i];
		av[n++] = "-o";
		av[n++] = "/dev/null";
		av[n] = NULL;
		ppd_run(n, av, &q);
	}
	return ppd_run(argc, argv, &e);
}

/*
 * vpp: the preprocessor alone (see ppdriver.c), for parity tests.
 *
 *   vpp [--cmp REF.i] [-o OUT] CLANG-ARGS...
 *   vpp --cmp-files OURS REF      compare two -E outputs
 *   vpp --batch JOBS [-j N] [-C DIR] [--cmp]
 *                                 many TUs in one process on N threads (one
 *                                 token cache, like vccd); JOBS has one TU per
 *                                 line: REF.i, then the args, all separated by
 *                                 \x1f.  Output is discarded unless --cmp
 *                                 (compare with REF.i).  Prints the wall time.
 *   VPP_WARM=N   N untimed passes first (fills the token cache; profiling)
 */
#include <fcntl.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "pp.h"
#include "ppdriver.h"

typedef struct job {
	char **av;
	int ac;
	int status;
	double ms;
} job;

static job *jobs;
static int njobs, cmpmode;
static _Atomic int nextjob;
static const char *bdir;

static double now(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec * 1e-9;
}

static void *worker(void *arg)
{
	int nul = open("/dev/null", O_WRONLY);

	(void)arg;
	for (;;) {
		int k = atomic_fetch_add(&nextjob, 1);
		job *j;
		ppd_env e = { bdir, nul, nul };
		double t0;

		if (k >= njobs)
			break;
		j = &jobs[k];
		t0 = now();
		j->status = ppd_run(j->ac, j->av, &e);
		j->ms = (now() - t0) * 1e3;
	}
	close(nul);
	return NULL;
}

static int batch(const char *file, int nthr)
{
	int fd = open(file, O_RDONLY);
	struct stat st;
	char *s, *e;
	pthread_t th[64];
	double t0, wall, sum = 0;
	int bad = 0;

	if (fd < 0 || fstat(fd, &st)) {
		perror(file);
		return 2;
	}
	s = malloc((size_t)st.st_size + 1);
	if (read(fd, s, (size_t)st.st_size) != st.st_size)
		return 2;
	s[st.st_size] = 0;
	close(fd);
	for (char *c = s; *c; c++)
		njobs += *c == '\n';
	jobs = calloc((size_t)njobs, sizeof(*jobs));
	njobs = 0;
	for (char *l = s; *l; l = e + 1) {
		int n = 1;
		char *ref, *c;
		job *j;

		e = strchr(l, '\n');
		if (!e)
			break;
		*e = 0;
		for (c = l; *c; c++)
			n += *c == 0x1f;
		j = &jobs[njobs++];
		j->av = calloc((size_t)n + 5, sizeof(char *));
		ref = strsep(&l, "\x1f");
		j->av[j->ac++] = "vpp";
		if (cmpmode) {
			j->av[j->ac++] = "--cmp";
			j->av[j->ac++] = ref;
		} else {
			j->av[j->ac++] = "-o";
			j->av[j->ac++] = "/dev/null";
		}
		while ((c = strsep(&l, "\x1f")))
			j->av[j->ac++] = c;
	}
	if (nthr > 64)
		nthr = 64;
	t0 = now();
	for (int i = 0; i < nthr; i++)
		pthread_create(&th[i], NULL, worker, NULL);
	for (int i = 0; i < nthr; i++)
		pthread_join(th[i], NULL);
	wall = now() - t0;
	for (int i = 0; i < njobs; i++) {
		sum += jobs[i].ms;
		if (jobs[i].status) {
			if (bad++ < 10)
				fprintf(stderr, "failed: %s\n", jobs[i].av[jobs[i].ac - 1]);
		}
	}
	printf("%d/%d ok; wall %.0f ms on %d threads, cpu sum %.0f ms\n", njobs - bad, njobs, wall * 1e3, nthr, sum);
	return bad != 0;
}

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
	if (argc >= 3 && !strcmp(argv[1], "--batch")) {
		int nthr = 6;

		for (int i = 3; i < argc; i++) {
			if (!strcmp(argv[i], "-j") && i + 1 < argc)
				nthr = atoi(argv[++i]);
			else if (!strcmp(argv[i], "-C") && i + 1 < argc)
				bdir = argv[++i];
			else if (!strcmp(argv[i], "--cmp"))
				cmpmode = 1;
		}
		return batch(argv[2], nthr);
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

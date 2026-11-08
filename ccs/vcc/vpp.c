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
 *       --warm N   N untimed passes over all jobs first (warm token cache,
 *                  header cache and declaration cache; outputs discarded)
 *       --stats F  one line per job to F: status, ms, instructions, cycles,
 *                  and instructions in cc_run / emit_tu / object write
 *                  (tab separated, last column the main file); vbench.py
 *   VPP_WARM=N   N untimed passes first (fills the token cache; profiling)
 */
#include <fcntl.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "pp.h"
#include "ccsnap.h"
#include "ppdriver.h"

typedef struct job {
	char **av;
	int ac;
	int status;
	double ms;
	vperf all, ph[PPD_NPH];
} job;

static job *jobs;
static int njobs, cmpmode, warmup;
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
		vperf v0, ph0[PPD_NPH];

		memcpy(ph0, ppd_ph, sizeof(ph0));
		t0 = now();
		v0 = vperf_now();
		j->status = ppd_run(j->ac, j->av, &e);
		j->all = vperf_sub(vperf_now(), v0);
		j->ms = (now() - t0) * 1e3;
		for (int i = 0; i < PPD_NPH; i++)
			j->ph[i] = vperf_sub(ppd_ph[i], ph0[i]);
	}
	close(nul);
	return NULL;
}

static void run_pass(int nthr)
{
	pthread_t th[64];

	atomic_store(&nextjob, 0);
	for (int i = 0; i < nthr; i++)
		pthread_create(&th[i], NULL, worker, NULL);
	for (int i = 0; i < nthr; i++)
		pthread_join(th[i], NULL);
}

static int batch(const char *file, int nthr, const char *statsf)
{
	int fd = open(file, O_RDONLY);
	struct stat st;
	char *s, *e;
	double t0, wall, sum = 0;
	vperf tot = { 0, 0 }, ph[PPD_NPH] = { { 0, 0 } };
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
	for (int w = 0; w < warmup; w++)
		run_pass(nthr);
	t0 = now();
	run_pass(nthr);
	wall = now() - t0;
	for (int i = 0; i < njobs; i++) {
		sum += jobs[i].ms;
		tot.ins += jobs[i].all.ins;
		tot.cyc += jobs[i].all.cyc;
		for (int k = 0; k < PPD_NPH; k++) {
			ph[k].ins += jobs[i].ph[k].ins;
			ph[k].cyc += jobs[i].ph[k].cyc;
		}
		if (jobs[i].status) {
			if (bad++ < 10)
				fprintf(stderr, "failed: %s\n", jobs[i].av[jobs[i].ac - 1]);
		}
	}
	{
		char hs[512];

		pp_hc_stats(hs, sizeof(hs));
		printf("%s\n", hs);
		dc_stats(hs, sizeof(hs));
		printf("%s\n", hs);
	}
	{
		struct rusage ru;

		getrusage(RUSAGE_SELF, &ru);
		printf("%d/%d ok; wall %.0f ms on %d threads, job sum %.0f ms; cpu user %.2f s sys %.2f s\n", njobs - bad, njobs,
		       wall * 1e3, nthr, sum, ru.ru_utime.tv_sec + ru.ru_utime.tv_usec * 1e-6, ru.ru_stime.tv_sec + ru.ru_stime.tv_usec * 1e-6);
		printf("perf: %llu instructions, %llu cycles (jobs); cc_run %llu, emit %llu, obj %llu instructions; emit %llu cycles\n",
		       (unsigned long long)tot.ins, (unsigned long long)tot.cyc, (unsigned long long)ph[PPD_PH_CC].ins,
		       (unsigned long long)ph[PPD_PH_EMIT].ins, (unsigned long long)ph[PPD_PH_OBJ].ins,
		       (unsigned long long)ph[PPD_PH_EMIT].cyc);
		if (ppd_bytes_obj)
			printf("-c: cc_run %.0f ms (codegen+as %.0f ms), finish+write %.0f ms, %.1f MB objects\n", ppd_ns_cc / 1e6,
			       ppd_ns_emit / 1e6, ppd_ns_obj / 1e6, ppd_bytes_obj / 1e6);
	}
	if (statsf) {
		FILE *f = fopen(statsf, "w");

		if (!f) {
			perror(statsf);
			return 2;
		}
		for (int i = 0; i < njobs; i++) {
			job *j = &jobs[i];
			const char *mf = j->av[j->ac - 1];

			for (int k = j->ac - 1; k > 0; k--)
				if (j->av[k][0] != '-' && (strstr(j->av[k], ".c") || strstr(j->av[k], ".S"))) {
					mf = j->av[k];
					break;
				}
			fprintf(f, "%d\t%.3f\t%llu\t%llu\t%llu\t%llu\t%llu\t%s\n", j->status, j->ms, (unsigned long long)j->all.ins,
				(unsigned long long)j->all.cyc, (unsigned long long)j->ph[PPD_PH_CC].ins,
				(unsigned long long)j->ph[PPD_PH_EMIT].ins, (unsigned long long)j->ph[PPD_PH_OBJ].ins, mf);
		}
		fclose(f);
	}
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
		const char *statsf = NULL;

		for (int i = 3; i < argc; i++) {
			if (!strcmp(argv[i], "-j") && i + 1 < argc)
				nthr = atoi(argv[++i]);
			else if (!strcmp(argv[i], "-C") && i + 1 < argc)
				bdir = argv[++i];
			else if (!strcmp(argv[i], "--cmp"))
				cmpmode = 1;
			else if (!strcmp(argv[i], "--warm") && i + 1 < argc)
				warmup = atoi(argv[++i]);
			else if (!strcmp(argv[i], "--stats") && i + 1 < argc)
				statsf = argv[++i];
		}
		return batch(argv[2], nthr, statsf);
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

/*
 * vpp: the preprocessor alone (the driver is driver_job_run in driver/job.c), for parity tests.
 *
 *   vpp [--cmp REF.i] [-o OUT] CLANG-ARGS...
 *   vpp --cmp-files OURS REF      compare two -E outputs
 *   vpp --lint [-fsyntax-only] CLANG-ARGS...
 *                                 the front end alone plus the style checks: each
 *                                 file-scope function and object defined in the
 *                                 main file must carry its module's prefix
 *                                 (frontend/c/naming-lint.c; the option is parsed
 *                                 in driver/cmdline.c, off in every other run)
 *   vpp --batch JOBS [-j N] [-C DIR] [--cmp]
 *                                 many TUs in one process on N threads (-j,
 *                                 else VCC_JOBS, else 6: the budget of the
 *                                 shared pool, nested emit included; one
 *                                 token cache, like vccd); JOBS has one TU per
 *                                 line: REF.i, then the args, all separated by
 *                                 \x1f.  Output is discarded unless --cmp
 *                                 (compare with REF.i).  Prints the wall time.
 *       --warm N   N untimed passes over all jobs first (warm token cache,
 *                  header cache and declaration cache; outputs discarded)
 *       --stats F  one line per job to F: status, ms, instructions, cycles,
 *                  and instructions in cc_translation_unit_run / codegen_unit_emit / object write
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

#include "frontend/pp/pp.h"
#include "driver/declaration-cache.h"
#include "driver/driver.h"
#include "port/task.h"

typedef struct vpp_job {
	char **argv;
	int argc;
	int status;
	double milliseconds;
	vperf total_counters, phase_counters[DRIVER_PHASE_COUNT];
} vpp_job;

static vpp_job *vpp_jobs;
static int vpp_job_count, vpp_compare_mode, vpp_warmup;
static const char *vpp_batch_directory;

static double vpp_clock_seconds(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec * 1e-9;
}

static int vpp_null_fd = -1;	/* /dev/null: the jobs' output and (without VPP_BATCH_ERR) diagnostics */

/* one TU of the batch, on whichever thread of the pool takes it */
static void vpp_job_run(void *arg, size_t k)
{
	vpp_job *j = &vpp_jobs[k];
	driver_environment e = { .cwd = vpp_batch_directory, .outfd = vpp_null_fd, .errfd = getenv("VPP_BATCH_ERR") ? 2 : vpp_null_fd };
	vperf v0, ph0[DRIVER_PHASE_COUNT];
	double t0;

	(void)arg;
	memcpy(ph0, driver_phase_counters, sizeof(ph0));	/* (per thread: the job's own work is the delta) */
	t0 = vpp_clock_seconds();
	v0 = vperformance_now();
	j->status = driver_job_run(j->argc, j->argv, &e);
	j->total_counters = vperformance_subtract(vperformance_now(), v0);
	j->milliseconds = (vpp_clock_seconds() - t0) * 1e3;
	for (int i = 0; i < DRIVER_PHASE_COUNT; i++)
		j->phase_counters[i] = vperformance_subtract(driver_phase_counters[i], ph0[i]);
}

/* every job once, nthr at a time: the TUs on the shared pool (port/task.c),
   whose threads get a stack as large as main's, so that a TU nests as deep
   in a batch as it does alone; emit helpers (VCC_THREADS) come from the same
   budget, so they only run when TUs leave threads idle */
static void vpp_pass_run(int nthr)
{
	vtask_parallel_for((size_t)vpp_job_count, nthr, vpp_job_run, NULL, 0);
}

static int vpp_batch_run(const char *file, int nthr, const char *statsf)
{
	int fd = open(file, O_RDONLY);
	struct stat st;
	char *s, *e;
	double t0, wall, sum = 0;
	vperf tot = { 0, 0 }, ph[DRIVER_PHASE_COUNT] = { { 0, 0 } };
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
		vpp_job_count += *c == '\n';
	vpp_jobs = calloc((size_t)vpp_job_count, sizeof(*vpp_jobs));
	vpp_job_count = 0;
	for (char *l = s; *l; l = e + 1) {
		int n = 1;
		char *ref, *c;
		vpp_job *j;

		e = strchr(l, '\n');
		if (!e)
			break;
		*e = 0;
		for (c = l; *c; c++)
			n += *c == 0x1f;
		j = &vpp_jobs[vpp_job_count++];
		j->argv = calloc((size_t)n + 5, sizeof(char *));
		ref = strsep(&l, "\x1f");
		j->argv[j->argc++] = "vpp";
		if (vpp_compare_mode) {
			j->argv[j->argc++] = "--cmp";
			j->argv[j->argc++] = ref;
		} else {
			j->argv[j->argc++] = "-o";
			j->argv[j->argc++] = "/dev/null";
		}
		while ((c = strsep(&l, "\x1f")))
			j->argv[j->argc++] = c;
	}
	if (nthr > 64)
		nthr = 64;
	if (nthr < 1)
		nthr = 1;
	vtask_budget_set(nthr);
	vpp_null_fd = open("/dev/null", O_WRONLY);
	for (int w = 0; w < vpp_warmup; w++)
		vpp_pass_run(nthr);
	t0 = vpp_clock_seconds();
	vpp_pass_run(nthr);
	wall = vpp_clock_seconds() - t0;
	for (int i = 0; i < vpp_job_count; i++) {
		sum += vpp_jobs[i].milliseconds;
		tot.ins += vpp_jobs[i].total_counters.ins;
		tot.cyc += vpp_jobs[i].total_counters.cyc;
		for (int k = 0; k < DRIVER_PHASE_COUNT; k++) {
			ph[k].ins += vpp_jobs[i].phase_counters[k].ins;
			ph[k].cyc += vpp_jobs[i].phase_counters[k].cyc;
		}
		if (vpp_jobs[i].status) {
			if (bad++ < 10)
				fprintf(stderr, "failed: %s\n", vpp_jobs[i].argv[vpp_jobs[i].argc - 1]);
		}
	}
	{
		char hs[512];

		pp_header_cache_statistics_format(hs, sizeof(hs));
		printf("%s\n", hs);
		driver_declaration_cache_statistics_format(hs, sizeof(hs));
		printf("%s\n", hs);
		cc_lazy_body_statistics(hs, sizeof(hs));
		printf("%s\n", hs);
		cc_replay_coverage_statistics(hs, sizeof(hs));
		if (*hs)
			printf("%s\n", hs);
	}
	{
		char ds[2048];

		cc_replay_statistics(ds, sizeof(ds));
		if (*ds)
			printf("%s\n", ds);
	}
	{
		struct rusage ru;

		getrusage(RUSAGE_SELF, &ru);
		printf("%d/%d ok; wall %.0f ms on %d threads, job sum %.0f ms; cpu user %.2f s sys %.2f s\n", vpp_job_count - bad, vpp_job_count,
		       wall * 1e3, nthr, sum, ru.ru_utime.tv_sec + ru.ru_utime.tv_usec * 1e-6, ru.ru_stime.tv_sec + ru.ru_stime.tv_usec * 1e-6);
		printf("perf: %llu instructions, %llu cycles (jobs); cc_run %llu, emit %llu, obj %llu instructions; emit %llu cycles\n",
		       (unsigned long long)tot.ins, (unsigned long long)tot.cyc, (unsigned long long)ph[DRIVER_PHASE_COMPILE].ins,
		       (unsigned long long)ph[DRIVER_PHASE_EMIT].ins, (unsigned long long)ph[DRIVER_PHASE_OBJECT].ins,
		       (unsigned long long)ph[DRIVER_PHASE_EMIT].cyc);
		if (driver_object_bytes)
			printf("-c: cc_run %.0f ms (codegen+as %.0f ms), finish+write %.0f ms, %.1f MB objects\n", driver_compile_nanoseconds / 1e6,
			       driver_emit_nanoseconds / 1e6, driver_object_nanoseconds / 1e6, driver_object_bytes / 1e6);
		if (driver_tokens_taken)
			printf("-c: %.1f M tokens to the parser, %.1f M skipped by the decl cache; %.0f instructions per token\n",
			       driver_tokens_taken / 1e6, driver_tokens_skipped / 1e6, (double)ph[DRIVER_PHASE_COMPILE].ins / (double)driver_tokens_taken);
	}
	if (statsf) {
		FILE *f = fopen(statsf, "w");

		if (!f) {
			perror(statsf);
			return 2;
		}
		for (int i = 0; i < vpp_job_count; i++) {
			vpp_job *j = &vpp_jobs[i];
			const char *mf = j->argv[j->argc - 1];

			for (int k = j->argc - 1; k > 0; k--)
				if (j->argv[k][0] != '-' && (strstr(j->argv[k], ".c") || strstr(j->argv[k], ".S"))) {
					mf = j->argv[k];
					break;
				}
			fprintf(f, "%d\t%.3f\t%llu\t%llu\t%llu\t%llu\t%llu\t%s\n", j->status, j->milliseconds, (unsigned long long)j->total_counters.ins,
				(unsigned long long)j->total_counters.cyc, (unsigned long long)j->phase_counters[DRIVER_PHASE_COMPILE].ins,
				(unsigned long long)j->phase_counters[DRIVER_PHASE_EMIT].ins, (unsigned long long)j->phase_counters[DRIVER_PHASE_OBJECT].ins, mf);
		}
		fclose(f);
	}
	return bad != 0;
}

int main(int argc, char **argv)
{
	driver_environment e = { .outfd = 1, .errfd = 2 };
	const char *w = getenv("VPP_WARM");

	if (argc == 4 && !strcmp(argv[1], "--cmp-files")) {
		int fd = open(argv[2], O_RDONLY);
		struct stat st;

		if (fd < 0 || fstat(fd, &st))
			return 2;
		return driver_tokens_compare(mmap(NULL, (size_t)st.st_size + 1, PROT_READ, MAP_PRIVATE, fd, 0),
				   (size_t)st.st_size, argv[3]);
	}
	if (argc >= 3 && !strcmp(argv[1], "--batch")) {
		int nthr = vtask_jobs_from_environment(6);
		const char *statsf = NULL;

		for (int i = 3; i < argc; i++) {
			if (!strcmp(argv[i], "-j") && i + 1 < argc)
				nthr = atoi(argv[++i]);
			else if (!strcmp(argv[i], "-C") && i + 1 < argc)
				vpp_batch_directory = argv[++i];
			else if (!strcmp(argv[i], "--cmp"))
				vpp_compare_mode = 1;
			else if (!strcmp(argv[i], "--warm") && i + 1 < argc)
				vpp_warmup = atoi(argv[++i]);
			else if (!strcmp(argv[i], "--stats") && i + 1 < argc)
				statsf = argv[++i];
		}
		return vpp_batch_run(argv[2], nthr, statsf);
	}

	for (int r = w ? atoi(w) : 0; r > 0; r--) {
		driver_environment q = { .outfd = -1, .errfd = 2 };
		char *av[argc + 3];
		int n = 0;

		for (int i = 0; i < argc; i++)
			av[n++] = argv[i];
		av[n++] = "-o";
		av[n++] = "/dev/null";
		av[n] = NULL;
		driver_job_run(n, av, &q);
	}
	return driver_job_run(argc, argv, &e);
}

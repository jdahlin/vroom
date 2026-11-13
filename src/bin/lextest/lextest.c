/*
 * lextest: lexer driver.
 *
 *   lextest dump FILE...          tokens in the normalized form lexdiff.py
 *                                 compares with clang -dump-raw-tokens
 *   lextest bench [-j N] [-r R] LIST
 *                                 lex every file in LIST (one path per line)
 *                                 through the token cache with N threads;
 *                                 -r: R times, uncached (for profiling)
 *                                 -p: vlex_prefetch() every file first
 *   lextest idcheck LIST          every identifier's id matches its spelling
 *   lextest core LIST             single thread, files pre-mapped and
 *                                 touched: pure lexing speed of one core
 */
#include <pthread.h>
#ifdef __APPLE__
#include <pthread/qos.h>
#else
#define pthread_set_qos_class_self_np(c, p) ((void)0)
#endif
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include "common/intern.h"
#include "frontend/lex/lex.h"
#include "port/darwin/pmc.h"
#include "port/performance-counters.h"

static double now(clockid_t c)
{
	struct timespec ts;

	clock_gettime(c, &ts);
	return ts.tv_sec + ts.tv_nsec * 1e-9;
}

/* llvm::write_escaped, as used by clang's token dump */
static void stringify(const char *s, size_t n)
{
	for (size_t i = 0; i < n; i++) {
		unsigned char c = (unsigned char)s[i];

		if (c == '\\' || c == '"')
			putchar('\\'), putchar(c);
		else if (c == '\t')
			fputs("\\t", stdout);
		else if (c == '\n')
			fputs("\\n", stdout);
		else if (c >= 0x20 && c < 0x7f)
			putchar(c);
		else
			printf("\\%03o", c);
	}
}

static int dump(const char *arg)
{
	char path[4096] = { 0 };	/* padded: vlex_get reads path[len + 7] */
	const vlex_file *f = strlen(arg) < sizeof(path) - 8 ? vlex_get(strcpy(path, arg), strlen(arg)) : NULL;
	size_t line = 1, bol = 0, pos = 0;
	char *buf;

	if (!f) {
		fprintf(stderr, "lextest: cannot read %s\n", path);
		return 1;
	}
	buf = malloc(f->size + 1);
	for (uint32_t i = 0; i < f->ntok; i++) {
		const vtok *t = &f->tok[i];
		size_t l;

		for (; pos < t->off; pos++) {
			char c = f->src[pos];

			if (c == '\n' || (c == '\r' && f->src[pos + 1] != '\n')) {
				line++;
				bol = pos + 1;
			}
		}
		l = vlex_spell(f->src, t, buf);
		printf("%s\t", vtok_name(t->kind));
		stringify(buf, l);
		printf("\t%zu:%zu\t%d\n", line, t->off - bol + 1, !!(t->flags & TF_SOL));
	}
	free(buf);
	return 0;
}

static char **paths;
static size_t *plen;
static size_t npaths;
static _Atomic size_t next_file;
static size_t reps = 1;	/* bench -r: uncached, every file reps times */
static int prefetch;	/* bench -p: vlex_prefetch() all files first */
static _Atomic uint64_t tot_bytes, tot_toks;
static double cpu_time[256];

static void read_list(const char *list)
{
	FILE *fp = fopen(list, "r");
	char line[4096];
	size_t cap = 0;

	if (!fp) {
		perror(list);
		exit(1);
	}
	while (fgets(line, sizeof(line), fp)) {
		size_t n = strcspn(line, "\n");

		if (!n)
			continue;
		line[n] = 0;
		if (npaths == cap) {
			cap = cap ? cap * 2 : 4096;
			paths = realloc(paths, cap * sizeof(*paths));
			plen = realloc(plen, cap * sizeof(*plen));
		}
		plen[npaths] = n;
		paths[npaths] = calloc(1, n + 8);	/* vlex_get hashes by words: path[n + 7] */
		memcpy(paths[npaths++], line, n);
	}
	fclose(fp);
}

static void *worker(void *arg)
{
	pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
	int id = (int)(intptr_t)arg;
	double c0 = now(CLOCK_THREAD_CPUTIME_ID);
	uint64_t b = 0, t = 0;
	size_t i;

	while ((i = atomic_fetch_add(&next_file, 1)) < npaths * reps) {
		vlex_file u;
		const vlex_file *f = reps > 1 ? (vlex_load_uncached(paths[i % npaths], &u) ? &u : NULL)
					      : vlex_get(paths[i], plen[i]);

		if (f) {
			b += f->size;
			t += f->ntok;
		}
	}
	cpu_time[id] = now(CLOCK_THREAD_CPUTIME_ID) - c0;
	atomic_fetch_add(&tot_bytes, b);
	atomic_fetch_add(&tot_toks, t);
	return NULL;
}

static int bench(int nthr, const char *list)
{
	pthread_t th[256];
	double t0, wall, cpu = 0;

	read_list(list);
	t0 = now(CLOCK_MONOTONIC);
	if (prefetch)
		for (size_t i = 0; i < npaths; i++)
			vlex_prefetch(paths[i], plen[i]);
	for (int i = 0; i < nthr; i++)
		pthread_create(&th[i], NULL, worker, (void *)(intptr_t)i);
	for (int i = 0; i < nthr; i++)
		pthread_join(th[i], NULL);
	wall = now(CLOCK_MONOTONIC) - t0;
	for (int i = 0; i < nthr; i++)
		cpu += cpu_time[i];
	printf("%zu files, %.1f MB, %.2f M tokens, %u identifiers\n", npaths,
	       tot_bytes / 1e6, tot_toks / 1e6, vintern_count());
	printf("wall %.1f ms with %d threads (%.2f GB/s), cpu %.1f ms (%.2f GB/s per core)\n",
	       wall * 1e3, nthr, tot_bytes / wall / 1e9, cpu * 1e3, tot_bytes / cpu / 1e9);
	return 0;
}

/* lexing only: files mapped and faulted in first, scratch and output reused */
static int core(const char *list, int reps)
{
	const char **src;
	size_t *sz, max = 0, bytes = 0, toks = 0;
	void *scratch;
	vtok *out;
	double t0, best = 1e9;

	read_list(list);
	src = calloc(npaths, sizeof(*src));
	sz = calloc(npaths, sizeof(*sz));
	for (size_t i = 0; i < npaths; i++) {
		const vlex_file *f = vlex_get(paths[i], plen[i]);	/* maps + warms */

		if (!f)
			continue;
		src[i] = f->src;
		sz[i] = f->size;
		max = sz[i] > max ? sz[i] : max;
		bytes += sz[i];
	}
	scratch = malloc(vlex_scratch_size(max));
	out = malloc((max + 2) * sizeof(vtok));
	vlex_no_intern = getenv("NOINTERN") ? atoi(getenv("NOINTERN")) : 0;
	int pmc = getenv("PMC") && !pmc_init(getenv("PMC"));
	uint64_t c0[PMC_N], c1[PMC_N], cb[PMC_N] = { 0 };
	vperf vbest = { ~0ull, ~0ull }, v0;
	uint64_t h = 1469598103934665603ull;

	for (int r = 0; r < reps; r++) {
		toks = 0;
		if (pmc)
			pmc_read(c0);
		v0 = vperformance_now();
		t0 = now(CLOCK_THREAD_CPUTIME_ID);
		for (size_t i = 0; i < npaths; i++)
			if (src[i])
				toks += vlex(src[i], sz[i], out, scratch);
		t0 = now(CLOCK_THREAD_CPUTIME_ID) - t0;
		v0 = vperformance_subtract(vperformance_now(), v0);
		vbest.ins = v0.ins < vbest.ins ? v0.ins : vbest.ins;
		vbest.cyc = v0.cyc < vbest.cyc ? v0.cyc : vbest.cyc;
		if (pmc)
			pmc_read(c1);
		if (t0 < best) {
			best = t0;
			for (int j = 0; j < PMC_N; j++)
				cb[j] = c1[j] - c0[j];
		}
	}
	/* untimed: a hash of every token record (bench/vbench.py's correctness check) */
	for (size_t i = 0; i < npaths; i++)
		if (src[i]) {
			size_t n = vlex(src[i], sz[i], out, scratch);
			const unsigned char *q = (const unsigned char *)out;

			for (size_t k = 0; k < n * sizeof(vtok); k++)
				h = (h ^ q[k]) * 1099511628211ull;
		}
	printf("%.1f MB, %.2f M tokens: best of %d %.1f ms = %.2f GB/s, %.1f ns/token\n",
	       bytes / 1e6, toks / 1e6, reps, best * 1e3, bytes / best / 1e9, best * 1e9 / toks);
	printf("perf: %llu instructions, %llu cycles (min of %d passes)\nhash: %016llx\n", (unsigned long long)vbest.ins,
	       (unsigned long long)vbest.cyc, reps, (unsigned long long)h);
	if (pmc)
		for (int j = 0; j < pmc_n; j++)
			printf("  %-28s %14llu  %8.3f /token  %8.4f /byte\n", pmc_names[j],
			       (unsigned long long)cb[j], (double)cb[j] / toks, (double)cb[j] / bytes);
	return 0;
}

/* every identifier's id spells the identifier: same spelling <=> same id */
static int idcheck(const char *list)
{
	char *buf = NULL;
	size_t cap = 0, bad = 0, n = 0;

	read_list(list);
	if (getenv("PREFETCH"))	/* through the I/O threads */
		for (size_t i = 0; i < npaths; i++)
			vlex_prefetch(paths[i], plen[i]);
	for (size_t i = 0; i < npaths; i++) {
		const vlex_file *f = vlex_get(paths[i], plen[i]);

		if (!f)
			continue;
		if (cap < f->size + 1)
			buf = realloc(buf, cap = f->size + 1);
		for (uint32_t j = 0; j < f->ntok; j++) {
			const vtok *t = &f->tok[j];
			size_t l;

			if (t->kind != T_IDENT)
				continue;
			n++;
			l = vlex_spell(f->src, t, buf);
			if (!t->id || t->id > vintern_count() || vintern_ents[t->id].len != l ||
			    memcmp(vintern_ents[t->id].s, buf, l)) {
				if (bad++ < 10)
					fprintf(stderr, "%s: token %u '%.*s' has id %u\n", paths[i], j,
						(int)l, buf, t->id);
			}
		}
	}
	printf("%zu identifiers, %zu bad ids, %u distinct\n", n, bad, vintern_count());
	return bad != 0;
}

int main(int argc, char **argv)
{
	/* P cores: E-core timings are meaningless here */
	pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
	vintern_init();
	if (argc >= 3 && !strcmp(argv[1], "dump")) {
		int rc = 0;

		for (int i = 2; i < argc; i++)
			rc |= dump(argv[i]);
		return rc;
	}
	if (argc >= 3 && !strcmp(argv[1], "bench")) {
		int nthr = 8, a = 2;

		for (;;) {
			if (a + 2 < argc && !strcmp(argv[a], "-j"))
				nthr = atoi(argv[a + 1]);
			else if (a + 2 < argc && !strcmp(argv[a], "-r"))
				reps = (size_t)atoi(argv[a + 1]);
			else if (a + 1 < argc && !strcmp(argv[a], "-p")) {
				prefetch = 1;
				a--;	/* no argument */
			}
			else
				break;
			a += 2;
		}
		if (nthr < 1 || nthr > 256 || a >= argc)
			return 2;
		return bench(nthr, argv[a]);
	}
	if (argc >= 3 && !strcmp(argv[1], "idcheck"))
		return idcheck(argv[2]);
	if (argc >= 3 && !strcmp(argv[1], "core"))
		return core(argv[2], argc > 3 ? atoi(argv[3]) : 5);
	fprintf(stderr, "usage: lextest dump FILE... | bench [-j N] LIST | core LIST [REPS]\n");
	return 2;
}

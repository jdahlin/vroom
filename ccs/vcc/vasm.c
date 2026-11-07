/*
 * vasm: assembler driver.  vasm [-o out.o] in.s  (input: preprocessed text)
 *       vasm --bench N in.s...  (assemble each file N times, report MB/s)
 */
#include "as.h"
#include "elf.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static const char *map(const char *path, size_t *n)
{
	int fd = open(path, O_RDONLY);
	if (fd < 0) { perror(path); exit(1); }
	struct stat st; fstat(fd, &st);
	*n = (size_t)st.st_size;
	const char *p = *n ? mmap(NULL, *n, PROT_READ, MAP_PRIVATE, fd, 0) : "";
	close(fd);
	return p;
}

static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }

int main(int argc, char **argv)
{
	const char *out = "a.out", *in = 0;
	int bench = 0, first = 1;
	if (argc > 2 && !strcmp(argv[1], "--bench")) { bench = atoi(argv[2]); first = 3; }
	if (argc > 1 && !strcmp(argv[1], "--trace")) { as_trace = 1; argv++; argc--; }
	As *a = as_new();
	ElfObj *o = elf_new();
	if (bench) {
		/* inputs mapped once and touched, so this measures the assembler */
		int nf = argc - first;
		const char **ps = calloc((size_t)nf, sizeof *ps);
		size_t *ns = calloc((size_t)nf, sizeof *ns), tin = 0, tout = 0;
		static uint8_t *buf; static size_t cap;
		volatile unsigned sink = 0;
		for (int i = 0; i < nf; i++) { ps[i] = map(argv[first + i], &ns[i]); for (size_t j = 0; j < ns[i]; j += 4096) sink += (unsigned char)ps[i][j]; }
		double best = 1e9;
		for (int r = 0; r < bench; r++) {
			double t0 = now();
			for (int i = 0; i < nf; i++) {
				const char *msg;
				if (as_run(a, o, ps[i], ns[i], argv[first + i], &msg)) { fprintf(stderr, "%s\n", msg); return 1; }
				size_t k = elf_finish(o);
				if (k > cap) { cap = k * 2; buf = realloc(buf, cap); }
				elf_emit(o, buf);
				if (!r) { tin += ns[i]; tout += k; }
			}
			double t = now() - t0;
			if (t < best) best = t;
		}
		uint64_t st, sb;
		as_stats(a, &st, &sb);
		st /= (uint64_t)bench; sb /= (uint64_t)bench;
		printf("%.3f ms/pass (best of %d): in %zu B %.1f MB/s, out %zu B %.1f MB/s, %llu statements %.1f M/s, %.1f MB/s expanded\n",
		       best * 1e3, bench, tin, tin / best / 1e6, tout, tout / best / 1e6,
		       (unsigned long long)st, st / best / 1e6, sb / best / 1e6);
		return 0;
	}
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-o") && i + 1 < argc) out = argv[++i];
		else in = argv[i];
	}
	if (!in) { fprintf(stderr, "usage: vasm [-o out.o] in.s\n"); return 2; }
	size_t n; const char *p = map(in, &n);
	const char *msg;
	if (as_run(a, o, p, n, in, &msg)) { fprintf(stderr, "%s\n", msg); return 1; }
	if (elf_write_file(o, out)) { fprintf(stderr, "%s: %s\n", in, o->err); return 1; }
	return 0;
}

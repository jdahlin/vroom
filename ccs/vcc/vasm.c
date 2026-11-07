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
		size_t tin = 0, tout = 0;
		double t0 = now();
		for (int r = 0; r < bench; r++)
			for (int i = first; i < argc; i++) {
				size_t n; const char *p = map(argv[i], &n);
				const char *msg;
				if (as_run(a, o, p, n, argv[i], &msg)) { fprintf(stderr, "%s\n", msg); return 1; }
				size_t k = elf_finish(o);
				static uint8_t *buf; static size_t cap;
				if (k > cap) { cap = k * 2; buf = realloc(buf, cap); }
				elf_emit(o, buf);
				tin += n; tout += k;
				munmap((void *)p, n);
			}
		double t = now() - t0;
		printf("%.3f ms/pass  in %.1f MB/s  out %.1f MB/s\n", t * 1e3 / bench, tin / t / 1e6, tout / t / 1e6);
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

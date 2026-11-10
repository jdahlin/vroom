/*
 * vasm: assembler driver.  vasm [-o out.o] in.s  (input: preprocessed text)
 *       vasm --target=wasm64-unknown-emscripten [-o out.o] in.s  (LLVM's wasm syntax)
 *       vasm --target=arm64-apple-macos [-o out.o] in.s  (Apple's syntax, a Mach-O object)
 *       vasm --bench N in.s...  (assemble each file N times, report MB/s)
 */
#include "as.h"
#include "../arch/wasm/as/wasmas.h"
#include "../obj/obj.h"
#include "../lib/vperf.h"
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
	else if (argc > 1 && !strcmp(argv[1], "--trace-all")) { as_trace = 2; argv++; argc--; }
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
		vperf vbest = { ~0ull, ~0ull };
		uint64_t h = 1469598103934665603ull;
		for (int r = 0; r < bench; r++) {
			double t0 = now();
			vperf v0 = vperf_now();
			for (int i = 0; i < nf; i++) {
				const char *msg;
				if (as_run(a, o, ps[i], ns[i], argv[first + i], &msg)) { fprintf(stderr, "%s\n", msg); return 1; }
				size_t k = elf_finish(o);
				if (k > cap) { cap = k * 2; buf = realloc(buf, cap); }
				elf_emit(o, buf);
				if (!r) {
					tin += ns[i]; tout += k;
					for (size_t b = 0; b < k; b++) h = (h ^ buf[b]) * 1099511628211ull;
				}
			}
			v0 = vperf_sub(vperf_now(), v0);
			if (r) {	/* the first pass also hashes the objects */
				vbest.ins = v0.ins < vbest.ins ? v0.ins : vbest.ins;
				vbest.cyc = v0.cyc < vbest.cyc ? v0.cyc : vbest.cyc;
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
		printf("perf: %llu instructions, %llu cycles (min of %d passes after the first)\nhash: %016llx\n",
		       (unsigned long long)vbest.ins, (unsigned long long)vbest.cyc, bench - 1, (unsigned long long)h);
		return 0;
	}
	int wasm = 0, macho = 0;
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-o") && i + 1 < argc) out = argv[++i];
		else if (!strncmp(argv[i], "--target=wasm", 13)) wasm = strncmp(argv[i] + 13, "32", 2) ? 64 : 32;
		else if (!strncmp(argv[i], "--target=", 9) && (strstr(argv[i], "darwin") || strstr(argv[i], "apple") ||
							       strstr(argv[i], "macos"))) macho = 1;
		else in = argv[i];
	}
	if (!in) { fprintf(stderr, "usage: vasm [--target=wasm64-...|arm64-apple-macos] [-o out.o] in.s\n"); return 2; }
	size_t n; const char *p = map(in, &n);
	const char *msg;
	if (wasm) {
		/* LLVM's wasm .s to a wasm object (arch/wasm/as, platform/wasm) */
		uint8_t *ob; size_t on; char err[512];
		if (wasm_assemble(p, n, wasm == 32, &ob, &on, err, sizeof err)) { fprintf(stderr, "%s: %s\n", in, err); return 1; }
		FILE *f = fopen(out, "wb");
		if (!f || fwrite(ob, 1, on, f) != on || fclose(f)) { perror(out); return 1; }
		free(ob);
		return 0;
	}
	if (macho) {
		/* Apple's dialect, as vpp -c for a Darwin target: back to ours, then the Mach-O writer */
		size_t dn;
		char *d = as_darwin_text(p, n, &dn);
		if (as_run(a, o, d, dn, in, &msg)) { fprintf(stderr, "%s\n", msg); return 1; }
		size_t on = macho_finish(o);
		if (!on || o->has_err) { fprintf(stderr, "%s: %s\n", in, o->err[0] ? o->err : "cannot lay out the object"); return 1; }
		uint8_t *ob = malloc(on);
		FILE *f = fopen(out, "wb");
		if (!ob) return 1;
		macho_emit(o, ob);
		if (!f || fwrite(ob, 1, on, f) != on || fclose(f)) { perror(out); return 1; }
		free(ob);
		free(d);
		return 0;
	}
	if (as_run(a, o, p, n, in, &msg)) { fprintf(stderr, "%s\n", msg); return 1; }
	if (elf_write_file(o, out)) { fprintf(stderr, "%s: %s\n", in, o->err); return 1; }
	return 0;
}

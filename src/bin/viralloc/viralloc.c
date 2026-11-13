/*
 * viralloc: VIR register allocation on a text unit, with the synthetic target
 * (synth.c), checked by vir_alloc_check (PLAN-vir.md V4).
 *
 *   viralloc [options] FILE
 *     --regs N,M[,C,D]  N integer and M float registers, the top C and D callee-saved (default 6,4,2,1)
 *     --x86             x86-64's fixed registers and two-address operations
 *     --pool W          the live-in pool limit in words (smaller forces linear mode)
 *     --sweep           also allocate and check at every register count from the given one down
 *                       to the minimum, with and without --x86 and callee-saved registers
 *     --time R          allocate R times and print ns per instruction (sweeps 5+6, and the loops)
 *     --bless           rewrite the expected part of FILE
 *     --quiet           print nothing on success (the random tests)
 *     --mutate K        K times: allocate, corrupt one random thing (an operand or result
 *                       register, an edit, an edge move, a slot), run the checker; prints how
 *                       many it caught (a test of the checker; some mutations are harmless)
 *
 * FILE is a unit, optionally followed by `; ---` and the expected output; directives in
 * the comments at the top: `; REGS: 4,2,1,0`, `; X86: yes`, `; POOL: 40`, `; SWEEP: yes`.
 * The unit is put in canonical form and verified.  Output: per function the
 * allocation (vir_alloc_print) and the checker's verdict; with --sweep one line
 * with the number of configurations checked.  Exit 0 when everything checks and
 * the output equals the expected part (when there is one), else 1.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "vir/alloc.h"
#include "vir/synth.h"

static char *slurp(const char *path, size_t *n)
{
	FILE *fp = fopen(path, "rb");
	char *b;
	long sz;

	if (!fp)
		return NULL;
	fseek(fp, 0, SEEK_END);
	sz = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	b = malloc((size_t)sz + 1);
	if (!b || (sz > 0 && fread(b, 1, (size_t)sz, fp) != (size_t)sz)) {
		fclose(fp);
		free(b);
		return NULL;
	}
	fclose(fp);
	b[sz] = 0;
	*n = (size_t)sz;
	return b;
}

static size_t find_sep(const char *s, size_t n)
{
	for (size_t i = 0; i < n;) {
		size_t e = i;

		while (e < n && s[e] != '\n')
			e++;
		if (e - i >= 5 && !memcmp(s + i, "; ---", 5))
			return i;
		i = e + 1;
	}
	return n;
}

static bool parse_regs(const char *s, uint32_t r[4])
{
	return sscanf(s, "%u,%u,%u,%u", &r[0], &r[1], &r[2], &r[3]) >= 2;
}

static double now(void)
{
	struct timespec t;

	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec + t.tv_nsec * 1e-9;
}

/* allocate and check one function with one configuration; 0 ok */
static uint32_t one(vir_fn *f, const vir_unit *u, const vir_loops *lp, uint32_t r[4], bool x86, uint64_t pool, vbuf *out,
		    bool print)
{
	vir_synth t;
	vir_alloc_out o;
	vir_aopt opt = { pool };
	char err[200];
	uint32_t nbad;

	if (!vir_synth_init(&t, r[0], r[1], r[2], r[3], x86, err, sizeof(err))) {
		vbuf_f(out, "viralloc: %s\n", err);
		return 1;
	}
	if (!vir_alloc(f, lp, &t.regs, &opt, &o)) {
		vbuf_f(out, "; alloc @%s regs %u,%u,%u,%u%s: failed: %s\n", vir_fname(u, f), r[0], r[1], r[2], r[3],
		       x86 ? " x86" : "", o.err);
		return 1;
	}
	if (print) {
		vbuf_f(out, "; alloc @%s regs %u,%u,%u,%u%s\n", vir_fname(u, f), r[0], r[1], r[2], r[3], x86 ? " x86" : "");
		vir_alloc_print(out, u, f, lp, &o);
	}
	{
		vbuf m = { 0 };

		nbad = vir_alloc_check(f, &t.regs, &o, &m);
		if (nbad) {
			if (!print) {
				vbuf_f(out, "; check @%s regs %u,%u,%u,%u%s pool %llu:\n", vir_fname(u, f), r[0], r[1], r[2], r[3],
				       x86 ? " x86" : "", (unsigned long long)pool);
			}
			vbuf_put(out, m.p, m.n);
		}
		free(m.p);
	}
	return nbad;
}

int main(int argc, char **argv)
{
	uint32_t regs[4] = { 6, 4, 2, 1 }, nbad = 0, rep = 0, mut = 0;
	bool x86 = false, sweep = false, bless = false, quiet = false;
	uint64_t pool = 0;
	const char *path = NULL;
	char *src;
	size_t n, sep;
	vir_unit u;
	vir_perr perr;
	vbuf out = { 0 };
	int rc = 1;

	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--regs") && i + 1 < argc) {
			if (!parse_regs(argv[++i], regs))
				goto usage;
		} else if (!strcmp(argv[i], "--x86")) {
			x86 = true;
		} else if (!strcmp(argv[i], "--sweep")) {
			sweep = true;
		} else if (!strcmp(argv[i], "--bless")) {
			bless = true;
		} else if (!strcmp(argv[i], "--quiet")) {
			quiet = true;
		} else if (!strcmp(argv[i], "--pool") && i + 1 < argc) {
			pool = strtoull(argv[++i], NULL, 0);
		} else if (!strcmp(argv[i], "--mutate") && i + 1 < argc) {
			mut = (uint32_t)atoi(argv[++i]);
		} else if (!strcmp(argv[i], "--time") && i + 1 < argc) {
			rep = (uint32_t)atoi(argv[++i]);
		} else if (argv[i][0] == '-' || path) {
			goto usage;
		} else {
			path = argv[i];
		}
	}
	if (!path)
		goto usage;
	src = slurp(path, &n);
	if (!src) {
		fprintf(stderr, "viralloc: cannot read %s\n", path);
		return 2;
	}
	sep = find_sep(src, n);
	for (size_t i = 0; i < sep;) {
		size_t e = i;

		while (e < sep && src[e] != '\n')
			e++;
		if (src[i] != ';' && e > i)
			break;
		if (!strncmp(src + i, "; REGS:", 7))
			parse_regs(src + i + 7, regs);
		else if (!strncmp(src + i, "; X86: yes", 10))
			x86 = true;
		else if (!strncmp(src + i, "; SWEEP: yes", 12))
			sweep = true;
		else if (!strncmp(src + i, "; POOL:", 7))
			pool = strtoull(src + i + 7, NULL, 0);
		i = e + 1;
	}
	vir_unit_init(&u);
	if (!vir_parse(&u, src, sep, &perr)) {
		vbuf_f(&out, "%s:%u:%u: %s\n", path, perr.line, perr.col, perr.msg);
		goto done;
	}
	for (uint32_t fi = 0; fi < u.nfn; fi++) {
		vir_fn f;
		vir_loops lp;
		vbuf ver = { 0 };

		vir_fn_load(&f, &u, fi);
		if (!vir_canon(&f)) {
			vbuf_f(&out, "@%s: no canonical form\n", vir_fname(&u, &f));
			nbad++;
			continue;
		}
		if (vir_verify(&f, &u, &ver, 10)) {
			vbuf_put(&out, ver.p, ver.n);
			free(ver.p);
			nbad++;
			continue;
		}
		if (!vir_loops_build(&f, &lp)) {
			vbuf_f(&out, "@%s: no loops\n", vir_fname(&u, &f));
			nbad++;
			continue;
		}
		nbad += one(&f, &u, &lp, regs, x86, pool, &out, !quiet);
		if (sweep) {
			uint32_t cnt = 0, fails = 0;

			for (uint32_t ni = regs[0]; ni >= 3; ni--)
				for (uint32_t nf = regs[1]; nf >= 2; nf--)
					for (uint32_t cs = 0; cs < 3; cs++)
						for (int xx = 0; xx < 2; xx++) {
							uint32_t r[4] = { ni, nf, cs == 0 ? 0 : cs == 1 ? (ni - 2 < 2 ? ni - 2 : 2) : ni - 2,
									  cs == 0 ? 0 : cs == 1 ? (nf - 2 < 1 ? nf - 2 : 1) : nf - 2 };
							uint32_t k = one(&f, &u, &lp, r, xx, pool, &out, false);

							cnt++;
							fails += k != 0;
							if (pool == 0 && !k) {	/* linear mode too */
								k = one(&f, &u, &lp, r, xx, 1, &out, false);
								cnt++;
								fails += k != 0;
							}
						}
			if (!quiet || fails)
				vbuf_f(&out, "; sweep @%s: %u configurations, %u failed\n", vir_fname(&u, &f), cnt, fails);
			nbad += fails;
		}
		if (mut) {
			vir_synth t;
			vir_alloc_out o;
			vir_aopt opt = { pool };
			char err[200];
			uint32_t caught = 0, tried = 0, seed = 12345;

			vir_synth_init(&t, regs[0], regs[1], regs[2], regs[3], x86, err, sizeof(err));
			for (uint32_t k = 0; k < mut; k++) {
				vbuf m = { 0 };
				uint32_t what;

				if (!vir_alloc(&f, &lp, &t.regs, &opt, &o))
					break;
#define RND() (seed = seed * 1103515245u + 12345u, seed >> 8)
				what = RND() % 4;
				if (what == 0 && o.nopr) {
					uint32_t x = RND() % o.nopr;
					uint8_t r = o.opr[x];

					if (r == VR_NONE || r >= 128)
						continue;
					o.opr[x] = VR_MAKE(VR_CLASS(r), (VR_NUM(r) + 1 + RND() % 3) % (VR_CLASS(r) ? regs[1] : regs[0]));
					if (o.opr[x] == r)
						continue;
				} else if (what == 1 && o.nedit) {
					vir_edit *e = &o.edit[RND() % o.nedit];

					if (e->kind == VE_SWAP)
						continue;
					if (e->kind == VE_STORE)	/* (its dst is unused: the register it stores from) */
						e->src = VR_MAKE(VR_CLASS(e->src), (VR_NUM(e->src) + 1) % (VR_CLASS(e->src) ? regs[1] : regs[0]));
					else
						e->dst = VR_MAKE(VR_CLASS(e->dst), (VR_NUM(e->dst) + 1) % (VR_CLASS(e->dst) ? regs[1] : regs[0]));
				} else if (what == 2 && o.nmove) {
					vir_emove *e = &o.move[RND() % o.nmove];

					if (e->dst & VL_SLOT || e->dst >= 128)
						continue;
					e->dst = VR_MAKE(VR_CLASS(e->dst), (VR_NUM(e->dst) + 1) % (VR_CLASS(e->dst) ? regs[1] : regs[0]));
				} else if (what == 3 && o.nspill > 1) {
					uint32_t v = f.nconst + RND() % (f.nins - f.nconst);

					if (o.slot[v] == VIR_NOBLK)
						continue;
					o.slot[v] = (o.slot[v] + 1) % o.nspill;
				} else {
					continue;
				}
#undef RND
				tried++;
				if (vir_alloc_check(&f, &t.regs, &o, &m))
					caught++;
				free(m.p);
			}
			vbuf_f(&out, "; mutate @%s: %u mutations, %u caught\n", vir_fname(&u, &f), tried, caught);
		}
		if (rep) {
			vir_synth t;
			vir_alloc_out o;
			vir_aopt opt = { pool };
			char err[200];
			double t0, best = 1e30, bestl = 1e30;
			uint32_t nb = 0;

			vir_synth_init(&t, regs[0], regs[1], regs[2], regs[3], x86, err, sizeof(err));
			for (uint32_t k = 0; k < rep; k++) {
				t0 = now();
				vir_loops_build(&f, &lp);
				double t1 = now();

				vir_alloc(&f, &lp, &t.regs, &opt, &o);
				double t2 = now();

				if (t2 - t1 < best)
					best = t2 - t1;
				if (t1 - t0 < bestl)
					bestl = t1 - t0;
			}
			for (uint32_t b = 0; b < f.nblk; b++)
				nb += f.blk[b].n;
			fprintf(stderr, "@%s: %u instructions, %u blocks, %u loops: alloc %.1f ns/ins (%.3f ms), loops %.1f ns/ins%s\n",
				vir_fname(&u, &f), nb, f.nblk, lp.n, best * 1e9 / nb, best * 1e3, bestl * 1e9 / nb,
				o.st.linear ? " (linear)" : "");
		}
	}
	rc = nbad ? 1 : 0;
done:
	if (!quiet || rc)
		fwrite(out.p, 1, out.n, stdout);
	if (sep < n && !bless) {
		const char *exp = src + sep;

		while (*exp && *exp != '\n')
			exp++;
		if (*exp)
			exp++;
		if (strlen(exp) != out.n || memcmp(exp, out.p, out.n)) {
			if (!quiet)
				fprintf(stderr, "viralloc: %s: output differs from the expected part\n", path);
			rc = 1;
		}
	}
	if (bless) {
		FILE *fp = fopen(path, "wb");

		if (fp) {
			fwrite(src, 1, sep, fp);
			if (sep && src[sep - 1] != '\n')
				fputc('\n', fp);
			fputs("; ---\n", fp);
			fwrite(out.p, 1, out.n, fp);
			fclose(fp);
		}
	}
	vir_unit_free(&u);
	free(out.p);
	free(src);
	return rc;
usage:
	fprintf(stderr, "usage: viralloc [--regs N,M[,C,D]] [--x86] [--pool W] [--sweep] [--time R] [--bless] [--quiet] FILE\n");
	return 2;
}

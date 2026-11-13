/*
 * viralloc: VIR register allocation on a text unit, with the synthetic target
 * (synthetic-target.c), checked by vir_alloc_check (PLAN-vir.md V4).
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

#include "vir/register-allocation.h"
#include "vir/synthetic-target.h"

static char *viralloc_file_read(const char *path, size_t *n)
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

static size_t viralloc_separator_find(const char *s, size_t n)
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

static bool viralloc_registers_parse(const char *s, uint32_t r[4])
{
	return sscanf(s, "%u,%u,%u,%u", &r[0], &r[1], &r[2], &r[3]) >= 2;
}

static double viralloc_time_now(void)
{
	struct timespec t;

	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec + t.tv_nsec * 1e-9;
}

/* allocate and check one function with one configuration; 0 ok */
static uint32_t viralloc_function_allocate(vir_function *f, const vir_unit *u, const vir_loops *lp, uint32_t r[4], bool x86, uint64_t pool, vbuffer *out,
		    bool print)
{
	vir_synthetic_target t;
	vir_alloc_out o;
	vir_alloc_options opt = { pool };
	char err[200];
	uint32_t nbad;

	if (!vir_synth_init(&t, r[0], r[1], r[2], r[3], x86, err, sizeof(err))) {
		vbuffer_format_append(out, "viralloc: %s\n", err);
		return 1;
	}
	if (!vir_alloc(f, lp, &t.registers, &opt, &o)) {
		vbuffer_format_append(out, "; alloc @%s regs %u,%u,%u,%u%s: failed: %s\n", vir_function_name(u, f), r[0], r[1], r[2], r[3],
		       x86 ? " x86" : "", o.error_message);
		return 1;
	}
	if (print) {
		vbuffer_format_append(out, "; alloc @%s regs %u,%u,%u,%u%s\n", vir_function_name(u, f), r[0], r[1], r[2], r[3], x86 ? " x86" : "");
		vir_alloc_print(out, u, f, lp, &o);
	}
	{
		vbuffer m = { 0 };

		nbad = vir_alloc_check(f, &t.registers, &o, &m);
		if (nbad) {
			if (!print) {
				vbuffer_format_append(out, "; check @%s regs %u,%u,%u,%u%s pool %llu:\n", vir_function_name(u, f), r[0], r[1], r[2], r[3],
				       x86 ? " x86" : "", (unsigned long long)pool);
			}
			vbuffer_append(out, m.p, m.n);
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
	vir_parse_error perr;
	vbuffer out = { 0 };
	int rc = 1;

	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--regs") && i + 1 < argc) {
			if (!viralloc_registers_parse(argv[++i], regs))
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
	src = viralloc_file_read(path, &n);
	if (!src) {
		fprintf(stderr, "viralloc: cannot read %s\n", path);
		return 2;
	}
	sep = viralloc_separator_find(src, n);
	for (size_t i = 0; i < sep;) {
		size_t e = i;

		while (e < sep && src[e] != '\n')
			e++;
		if (src[i] != ';' && e > i)
			break;
		if (!strncmp(src + i, "; REGS:", 7))
			viralloc_registers_parse(src + i + 7, regs);
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
		vbuffer_format_append(&out, "%s:%u:%u: %s\n", path, perr.line, perr.col, perr.message);
		goto done;
	}
	for (uint32_t fi = 0; fi < u.function_count; fi++) {
		vir_function f;
		vir_loops lp;
		vbuffer ver = { 0 };

		vir_function_load(&f, &u, fi);
		if (!vir_canon(&f)) {
			vbuffer_format_append(&out, "@%s: no canonical form\n", vir_function_name(&u, &f));
			nbad++;
			continue;
		}
		if (vir_verify(&f, &u, &ver, 10)) {
			vbuffer_append(&out, ver.p, ver.n);
			free(ver.p);
			nbad++;
			continue;
		}
		if (!vir_loops_build(&f, &lp)) {
			vbuffer_format_append(&out, "@%s: no loops\n", vir_function_name(&u, &f));
			nbad++;
			continue;
		}
		nbad += viralloc_function_allocate(&f, &u, &lp, regs, x86, pool, &out, !quiet);
		if (sweep) {
			uint32_t cnt = 0, fails = 0;

			for (uint32_t ni = regs[0]; ni >= 3; ni--)
				for (uint32_t nf = regs[1]; nf >= 2; nf--)
					for (uint32_t cs = 0; cs < 3; cs++)
						for (int xx = 0; xx < 2; xx++) {
							uint32_t r[4] = { ni, nf, cs == 0 ? 0 : cs == 1 ? (ni - 2 < 2 ? ni - 2 : 2) : ni - 2,
									  cs == 0 ? 0 : cs == 1 ? (nf - 2 < 1 ? nf - 2 : 1) : nf - 2 };
							uint32_t k = viralloc_function_allocate(&f, &u, &lp, r, xx, pool, &out, false);

							cnt++;
							fails += k != 0;
							if (pool == 0 && !k) {	/* linear mode too */
								k = viralloc_function_allocate(&f, &u, &lp, r, xx, 1, &out, false);
								cnt++;
								fails += k != 0;
							}
						}
			if (!quiet || fails)
				vbuffer_format_append(&out, "; sweep @%s: %u configurations, %u failed\n", vir_function_name(&u, &f), cnt, fails);
			nbad += fails;
		}
		if (mut) {
			vir_synthetic_target t;
			vir_alloc_out o;
			vir_alloc_options opt = { pool };
			char err[200];
			uint32_t caught = 0, tried = 0, seed = 12345;

			vir_synth_init(&t, regs[0], regs[1], regs[2], regs[3], x86, err, sizeof(err));
			for (uint32_t k = 0; k < mut; k++) {
				vbuffer m = { 0 };
				uint32_t what;

				if (!vir_alloc(&f, &lp, &t.registers, &opt, &o))
					break;
#define VIRALLOC_RANDOM() (seed = seed * 1103515245u + 12345u, seed >> 8)
				what = VIRALLOC_RANDOM() % 4;
				if (what == 0 && o.operand_register_count) {
					uint32_t x = VIRALLOC_RANDOM() % o.operand_register_count;
					uint8_t r = o.operand_registers[x];

					if (r == VIR_REGISTER_NONE || r >= 128)
						continue;
					o.operand_registers[x] = VIR_REGISTER_MAKE(VIR_REGISTER_CLASS(r), (VIR_REGISTER_NUMBER(r) + 1 + VIRALLOC_RANDOM() % 3) % (VIR_REGISTER_CLASS(r) ? regs[1] : regs[0]));
					if (o.operand_registers[x] == r)
						continue;
				} else if (what == 1 && o.edit_count) {
					vir_edit *e = &o.edit[VIRALLOC_RANDOM() % o.edit_count];

					if (e->kind == VIR_EDIT_SWAP)
						continue;
					if (e->kind == VIR_EDIT_STORE)	/* (its dst is unused: the register it stores from) */
						e->src = VIR_REGISTER_MAKE(VIR_REGISTER_CLASS(e->src), (VIR_REGISTER_NUMBER(e->src) + 1) % (VIR_REGISTER_CLASS(e->src) ? regs[1] : regs[0]));
					else
						e->dst = VIR_REGISTER_MAKE(VIR_REGISTER_CLASS(e->dst), (VIR_REGISTER_NUMBER(e->dst) + 1) % (VIR_REGISTER_CLASS(e->dst) ? regs[1] : regs[0]));
				} else if (what == 2 && o.move_count) {
					vir_edge_move *e = &o.move[VIRALLOC_RANDOM() % o.move_count];

					if (e->dst & VIR_LOCATION_SLOT || e->dst >= 128)
						continue;
					e->dst = VIR_REGISTER_MAKE(VIR_REGISTER_CLASS(e->dst), (VIR_REGISTER_NUMBER(e->dst) + 1) % (VIR_REGISTER_CLASS(e->dst) ? regs[1] : regs[0]));
				} else if (what == 3 && o.spill_count > 1) {
					uint32_t v = f.constant_count + VIRALLOC_RANDOM() % (f.insn_count - f.constant_count);

					if (o.slot[v] == VIR_BLOCK_NONE)
						continue;
					o.slot[v] = (o.slot[v] + 1) % o.spill_count;
				} else {
					continue;
				}
#undef VIRALLOC_RANDOM
				tried++;
				if (vir_alloc_check(&f, &t.registers, &o, &m))
					caught++;
				free(m.p);
			}
			vbuffer_format_append(&out, "; mutate @%s: %u mutations, %u caught\n", vir_function_name(&u, &f), tried, caught);
		}
		if (rep) {
			vir_synthetic_target t;
			vir_alloc_out o;
			vir_alloc_options opt = { pool };
			char err[200];
			double t0, best = 1e30, bestl = 1e30;
			uint32_t nb = 0;

			vir_synth_init(&t, regs[0], regs[1], regs[2], regs[3], x86, err, sizeof(err));
			for (uint32_t k = 0; k < rep; k++) {
				t0 = viralloc_time_now();
				vir_loops_build(&f, &lp);
				double t1 = viralloc_time_now();

				vir_alloc(&f, &lp, &t.registers, &opt, &o);
				double t2 = viralloc_time_now();

				if (t2 - t1 < best)
					best = t2 - t1;
				if (t1 - t0 < bestl)
					bestl = t1 - t0;
			}
			for (uint32_t b = 0; b < f.block_count; b++)
				nb += f.blocks[b].insn_count;
			fprintf(stderr, "@%s: %u instructions, %u blocks, %u loops: alloc %.1f ns/ins (%.3f ms), loops %.1f ns/ins%s\n",
				vir_function_name(&u, &f), nb, f.block_count, lp.count, best * 1e9 / nb, best * 1e3, bestl * 1e9 / nb,
				o.statistics.linear ? " (linear)" : "");
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

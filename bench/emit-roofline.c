/*
 * Roofline for the code generator: what this CPU does when all it has to do
 * is write the output.  Two loops, both store-bound:
 *   - machine code: one 4-byte word per instruction (a direct encoder's floor);
 *   - assembly text: one ~24-byte line per instruction, copied from a small
 *     table of lines (the text printer's floor).
 * The buffer is touched first so page faults are not measured, then each
 * loop runs over the same bytes many times; the best run counts.
 *
 *   cc -O2 -o emit-roofline bench/emit-roofline.c && ./emit-roofline [instructions]
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static double now(void)
{
	struct timespec t;

	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec + t.tv_nsec * 1e-9;
}

static const char *lines[8] = {
	"\tldr\tx9, [sp, #24]\n", "\tadd\tx9, x9, #1\n", "\tstr\tw9, [x10, #8]\n",
	"\tbl\tsome_function_name\n", "\tcbz\tx9, .Ltmp.17\n", "\tmov\tx0, x19\n",
	"\tadrp\tx10, jiffies\n", "\tsubs\twzr, w9, #4\n",
};

int main(int argc, char **argv)
{
	size_t n = argc > 1 ? strtoul(argv[1], 0, 0) : 30000;
	uint32_t *code = malloc(n * 4);
	char *text = malloc(n * 32);
	size_t len[8];
	double best_c = 1e9, best_t = 1e9;
	size_t tn = 0;

	for (int k = 0; k < 8; k++)
		len[k] = strlen(lines[k]);
	memset(code, 1, n * 4);
	memset(text, 1, n * 32);
	for (int rep = 0; rep < 200; rep++) {
		double t0 = now(), t1, t2;
		char *p = text;

		for (size_t i = 0; i < n; i++)
			code[i] = 0xd2800000u | (uint32_t)(i & 0xffff) << 5 | (uint32_t)(i & 31);
		t1 = now();
		for (size_t i = 0; i < n; i++) {
			const char *l = lines[i & 7];

			memcpy(p, l, 32);	/* fixed-size copy, then advance by the real length */
			p += len[i & 7];
		}
		t2 = now();
		tn = (size_t)(p - text);
		if (t1 - t0 < best_c) best_c = t1 - t0;
		if (t2 - t1 < best_t) best_t = t2 - t1;
	}
	__asm__ volatile("" :: "r"(code), "r"(text) : "memory");
	printf("%zu instructions: code %.1f us (%.0f MB/s, %.2f ns/ins); text %.1f us for %.2f MB (%.0f MB/s of code, %.2f ns/ins)\n",
	       n, best_c * 1e6, n * 4 / best_c / 1e6, best_c * 1e9 / n,
	       best_t * 1e6, tn / 1e6, n * 4 / best_t / 1e6, best_t * 1e9 / n);
	return 0;
}

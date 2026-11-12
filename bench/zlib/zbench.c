/*
 * zbench FILE [REPS [LEVEL]]: times zlib's hot loops on one file, for bench/zlib/run.py
 * (LEVEL: only deflate and inflate at that level, for profiling).
 *
 * One line per measurement: NAME SECONDS BYTES CHECK, where SECONDS is the
 * best CPU time of REPS runs, BYTES the output size and CHECK a CRC of the
 * output, so two builds of zlib can be compared for both speed and result
 * (deflate is deterministic: the same zlib gives the same bytes).
 * Exit status 1 when a round trip does not give the input back.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "zlib.h"

static double now(void)
{
	struct timespec t;

	clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &t);
	return t.tv_sec + t.tv_nsec * 1e-9;
}

static void line(const char *name, int level, double s, unsigned long n, unsigned long check)
{
	if (level >= 0)
		printf("%s-%d %.6f %lu %08lx\n", name, level, s, n, check);
	else
		printf("%s %.6f %lu %08lx\n", name, s, n, check);
}

int main(int argc, char **argv)
{
	static const int levels[] = { 1, 6, 9 };
	int reps = argc > 2 ? atoi(argv[2]) : 3, only = argc > 3 ? atoi(argv[3]) : 0, bad = 0;
	unsigned char *in, *z, *out;
	unsigned long n, zcap, zn = 0, on, sum = 0;
	double best, t;
	FILE *f;
	long len;

	if (argc < 2 || !(f = fopen(argv[1], "rb"))) {
		fprintf(stderr, "usage: zbench FILE [REPS]\n");
		return 2;
	}
	fseek(f, 0, SEEK_END);
	len = ftell(f);
	rewind(f);
	n = (unsigned long)len;
	zcap = compressBound(n);
	in = malloc(n + 1);
	z = malloc(zcap);
	out = malloc(n + 1);
	if (!in || !z || !out || fread(in, 1, n, f) != n) {
		fprintf(stderr, "zbench: cannot read %s\n", argv[1]);
		return 2;
	}
	fclose(f);

	for (unsigned i = 0; i < sizeof levels / sizeof *levels; i++) {
		if (only && levels[i] != only)
			continue;
		best = 1e9;
		for (int r = 0; r < reps; r++) {
			zn = zcap;
			t = now();
			if (compress2(z, &zn, in, n, levels[i]) != Z_OK)
				bad = 1;
			t = now() - t;
			if (t < best)
				best = t;
		}
		line("deflate", levels[i], best, zn, crc32(0, z, (uInt)zn));
		best = 1e9;
		for (int r = 0; r < reps * 4; r++) {
			on = n;
			t = now();
			if (uncompress(out, &on, z, zn) != Z_OK)
				bad = 1;
			t = now() - t;
			if (t < best)
				best = t;
		}
		if (on != n || memcmp(in, out, n))
			bad = 1;
		line("inflate", levels[i], best, on, crc32(0, out, (uInt)on));
	}

	if (only)
		return bad;
	best = 1e9;
	for (int r = 0; r < reps; r++) {
		t = now();
		for (int k = 0; k < 32; k++)
			sum = crc32(k ? sum : 0, in, (uInt)n);
		t = now() - t;
		if (t < best)
			best = t;
	}
	line("crc32x32", -1, best, n, sum);
	best = 1e9;
	for (int r = 0; r < reps; r++) {
		t = now();
		for (int k = 0; k < 32; k++)
			sum = adler32(k ? sum : 1, in, (uInt)n);
		t = now() - t;
		if (t < best)
			best = t;
	}
	line("adler32x32", -1, best, n, sum);
	if (bad)
		fprintf(stderr, "zbench: round trip failed\n");
	return bad;
}

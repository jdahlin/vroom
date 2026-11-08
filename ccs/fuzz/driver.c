/*
 * Standalone driver for the fuzz targets, for a clang without libFuzzer
 * (Apple clang).  Same entry points (LLVMFuzzerTestOneInput, optional
 * LLVMFuzzerInitialize); a subset of libFuzzer's command line:
 *
 *   fuzz-X FILE|DIR...                  replay (like libFuzzer with -runs=0)
 *   fuzz-X -runs=N|-max_total_time=S [-max_len=L] [-dict=F] [-seed=K] DIR...
 *                                       mutate inputs from the corpus dirs;
 *                                       new inputs are not kept (no coverage
 *                                       feedback), a crash is saved as
 *                                       crash-<hash> (ASan death callback)
 *
 * The Makefile links this only when libclang_rt.fuzzer is missing.
 */
#include <dirent.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "fuzz.h"

__attribute__((weak)) int LLVMFuzzerInitialize(int *argc, char ***argv);
void __sanitizer_set_death_callback(void (*cb)(void));

typedef struct { uint8_t *p; size_t n; } buf;

static buf *corp, *dict;
static size_t ncorp, ndict;
static const uint8_t *cur;
static size_t curn;

static uint64_t rng = 0x9E3779B97F4A7C15ull;
static uint64_t rnd(void)
{
	rng ^= rng << 13, rng ^= rng >> 7, rng ^= rng << 17;
	return rng;
}

static void push(buf **v, size_t *n, uint8_t *p, size_t l)
{
	if (!(*n & (*n + 1)) || !*v)
		*v = realloc(*v, (*n * 2 + 2) * sizeof(buf));
	(*v)[(*n)++] = (buf){ p, l };
}

static int slurp(const char *path, uint8_t **p, size_t *n)
{
	FILE *f = fopen(path, "rb");
	long l;

	if (!f)
		return -1;
	fseek(f, 0, SEEK_END);
	l = ftell(f);
	fseek(f, 0, SEEK_SET);
	*p = malloc((size_t)l + 1);
	*n = fread(*p, 1, (size_t)l, f);
	fclose(f);
	return 0;
}

static void add_path(const char *path)
{
	struct stat st;
	uint8_t *p;
	size_t n;

	if (stat(path, &st))
		return;
	if (S_ISDIR(st.st_mode)) {
		DIR *d = opendir(path);
		struct dirent *e;

		while (d && (e = readdir(d))) {
			char sub[4096];

			if (e->d_name[0] == '.')
				continue;
			snprintf(sub, sizeof(sub), "%s/%s", path, e->d_name);
			add_path(sub);
		}
		if (d)
			closedir(d);
	} else if (!slurp(path, &p, &n)) {
		push(&corp, &ncorp, p, n);
	}
}

/* libFuzzer dictionary: name="value" lines with \\, \" and \xNN escapes */
static void load_dict(const char *path)
{
	uint8_t *s, *e, *l;
	size_t n;

	if (slurp(path, &s, &n))
		return;
	s[n] = 0;
	for (l = s; l < s + n; l = e + 1) {
		uint8_t *q, *o, *v;

		e = (uint8_t *)strchr((char *)l, '\n');
		if (!e)
			e = s + n;
		*e = 0;
		q = (uint8_t *)strchr((char *)l, '"');
		if (!q || l[0] == '#')
			continue;
		v = o = malloc((size_t)(e - q));
		for (q++; *q && *q != '"'; q++) {
			if (*q == '\\' && q[1] == 'x' && q[2] && q[3]) {
				char h[3] = { (char)q[2], (char)q[3], 0 };

				*o++ = (uint8_t)strtol(h, NULL, 16);
				q += 3;
			} else if (*q == '\\' && q[1]) {
				*o++ = *++q;
			} else {
				*o++ = *q;
			}
		}
		push(&dict, &ndict, v, (size_t)(o - v));
	}
}

static void save_crash(void)
{
	char name[64];
	uint64_t h = 1469598103934665603ull;
	FILE *f;

	if (!cur)
		return;
	for (size_t i = 0; i < curn; i++)
		h = (h ^ cur[i]) * 1099511628211ull;
	snprintf(name, sizeof(name), "crash-%016llx", (unsigned long long)h);
	if ((f = fopen(name, "wb"))) {
		fwrite(cur, 1, curn, f);
		fclose(f);
		fprintf(stderr, "driver: input saved as %s\n", name);
	}
}

static void on_signal(int sig)
{
	save_crash();
	signal(sig, SIG_DFL);
	raise(sig);
}

/* one random edit of b (capacity cap) */
static size_t mutate(uint8_t *b, size_t n, size_t cap)
{
	size_t a = n ? rnd() % n : 0, l = 1 + rnd() % 8;

	switch (rnd() % 7) {
	case 0:	/* flip a bit */
		if (n)
			b[a] ^= (uint8_t)(1u << (rnd() % 8));
		return n;
	case 1:	/* random byte */
		if (n)
			b[a] = (uint8_t)rnd();
		return n;
	case 2:	/* delete */
		if (a + l > n)
			l = n - a;
		memmove(b + a, b + a + l, n - a - l);
		return n - l;
	case 3:	/* duplicate a range */
		if (a + l > n)
			l = n - a;
		if (n + l > cap)
			return n;
		memmove(b + a + l, b + a, n - a);
		return n + l;
	case 4:
	case 5: {	/* insert a dictionary token */
		const buf *t;

		if (!ndict)
			return n;
		t = &dict[rnd() % ndict];
		if (n + t->n > cap)
			return n;
		memmove(b + a + t->n, b + a, n - a);
		memcpy(b + a, t->p, t->n);
		return n + t->n;
	}
	default: {	/* splice in a piece of another input */
		const buf *o = &corp[rnd() % ncorp];
		size_t f, k;

		if (!o->n)
			return n;
		f = rnd() % o->n;
		k = 1 + rnd() % (o->n - f);
		if (k > 64)
			k = 64;
		if (n + k > cap)
			return n;
		memmove(b + a + k, b + a, n - a);
		memcpy(b + a, o->p + f, k);
		return n + k;
	}
	}
}

int main(int argc, char **argv)
{
	long runs = -1, maxtime = 0;
	size_t maxlen = 4096;
	int nfiles = 0;

	if (LLVMFuzzerInitialize)
		LLVMFuzzerInitialize(&argc, &argv);
	for (int i = 1; i < argc; i++) {
		const char *a = argv[i];

		if (!strncmp(a, "-runs=", 6))
			runs = atol(a + 6);
		else if (!strncmp(a, "-max_total_time=", 16))
			maxtime = atol(a + 16);
		else if (!strncmp(a, "-max_len=", 9))
			maxlen = (size_t)atol(a + 9);
		else if (!strncmp(a, "-dict=", 6))
			load_dict(a + 6);
		else if (!strncmp(a, "-seed=", 6))
			rng = (uint64_t)atoll(a + 6) | 1;
		else if (a[0] == '-')
			fprintf(stderr, "driver: ignoring %s\n", a);
		else
			add_path(a), nfiles++;
	}
	__sanitizer_set_death_callback(save_crash);
	signal(SIGALRM, on_signal);
	signal(SIGABRT, on_signal);
	signal(SIGSEGV, on_signal);
	signal(SIGBUS, on_signal);
	signal(SIGILL, on_signal);
	signal(SIGTRAP, on_signal);

	if ((runs < 0 && !maxtime) || runs == 0) {
		/* replay */
		for (size_t i = 0; i < ncorp; i++) {
			cur = corp[i].p, curn = corp[i].n;
			alarm(10);
			LLVMFuzzerTestOneInput(cur, curn);
		}
		alarm(0);
		fprintf(stderr, "driver: replayed %zu inputs\n", ncorp);
		return 0;
	}
	if (!ncorp)
		push(&corp, &ncorp, calloc(1, 1), 0);
	{
		uint8_t *b = malloc(maxlen + 1);
		time_t t0 = time(NULL);

		for (long r = 0; runs < 0 || r < runs; r++) {
			const buf *s = &corp[rnd() % ncorp];
			size_t n = s->n < maxlen ? s->n : maxlen;
			int k = 1 + (int)(rnd() % 4);

			memcpy(b, s->p, n);
			while (k--)
				n = mutate(b, n, maxlen);
			cur = b, curn = n;
			alarm(10);
			LLVMFuzzerTestOneInput(b, n);
			if (maxtime && !(r & 255) && time(NULL) - t0 >= maxtime)
				break;
			if (!(r & 0xffff))
				fprintf(stderr, "driver: %ld runs\n", r);
		}
	}
	return 0;
}

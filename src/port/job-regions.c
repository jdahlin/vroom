/*
 * The debug mode of the job regions (job-regions.h): VCC_VMREG_CHECK and
 * VCC_VMREG_STATS.  The table of per-region counters is one per process.
 */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "port/job-regions.h"

int vjob_region_debug_mode = -1;	/* bit 0 check, bit 1 stats */

static struct { const char *name; unsigned long long res, puts, bad; } vjob_region_debug_table[64];
static pthread_mutex_t vjob_region_debug_mutex = PTHREAD_MUTEX_INITIALIZER;

static void vjob_region_debug_report(void)
{
	for (int i = 0; i < 64 && vjob_region_debug_table[i].name; i++)
		fprintf(stderr, "vmreg %-14s %6llu puts, %9.1f MB resident at put, %llu bad\n", vjob_region_debug_table[i].name,
			vjob_region_debug_table[i].puts, vjob_region_debug_table[i].res / 1e6, vjob_region_debug_table[i].bad);
}

int vjob_region_debug_init(void)
{
	int z = -1, m = (getenv("VCC_VMREG_CHECK") ? 1 : 0) | (getenv("VCC_VMREG_STATS") ? 2 : 0);

	if (__atomic_compare_exchange_n(&vjob_region_debug_mode, &z, m, 0, __ATOMIC_RELAXED, __ATOMIC_RELAXED) && (m & 2))
		atexit(vjob_region_debug_report);
	return m;
}

void vjob_region_debug_release(vjob_region *r, size_t used)
{
	size_t pg = (size_t)getpagesize(), lim = r->cap < ((size_t)8 << 30) ? r->cap : ((size_t)8 << 30);
	size_t np = lim / pg, res = 0, bad = 0;
	char *vec = malloc(np);
	int i;

	mincore(r->p, lim, (void *)vec);	/* char * on Darwin, unsigned char * on Linux */
	for (size_t k = 0; k < np; k++) {
		char *pp = r->p + k * pg;

		if (!(vec[k] & 1))
			continue;
		res += pg;
		if (used == VJOB_REGION_SCRATCH) {
			if (vjob_region_debug_mode & 1)
				memset(pp, 0xa5, pg);
		} else if ((vjob_region_debug_mode & 1) && k * pg + pg > used) {
			size_t from = k * pg < used ? used - k * pg : 0;

			for (size_t j = from; j < pg; j++)
				if (pp[j]) {
					if (!bad)
						fprintf(stderr, "vmreg %s: nonzero byte at %zu, past used %zu\n", r->name,
							k * pg + j, used);
					bad++;
					break;
				}
		}
	}
	free(vec);
	pthread_mutex_lock(&vjob_region_debug_mutex);
	for (i = 0; i < 63 && vjob_region_debug_table[i].name && strcmp(vjob_region_debug_table[i].name, r->name); i++)
		;
	vjob_region_debug_table[i].name = r->name;
	vjob_region_debug_table[i].res += res;
	vjob_region_debug_table[i].puts++;
	vjob_region_debug_table[i].bad += bad;
	pthread_mutex_unlock(&vjob_region_debug_mutex);
	if (bad && (vjob_region_debug_mode & 1))
		VBUG("a job region holds a nonzero byte past the bytes given back (VCC_VMREG_CHECK)");
}

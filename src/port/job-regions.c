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

int vmreg_dbg_mode = -1;	/* bit 0 check, bit 1 stats */

static struct { const char *name; unsigned long long res, puts, bad; } vmreg_dbg_tab[64];
static pthread_mutex_t vmreg_dbg_mu = PTHREAD_MUTEX_INITIALIZER;

static void vmreg_dbg_report(void)
{
	for (int i = 0; i < 64 && vmreg_dbg_tab[i].name; i++)
		fprintf(stderr, "vmreg %-14s %6llu puts, %9.1f MB resident at put, %llu bad\n", vmreg_dbg_tab[i].name,
			vmreg_dbg_tab[i].puts, vmreg_dbg_tab[i].res / 1e6, vmreg_dbg_tab[i].bad);
}

int vmreg_dbg_init(void)
{
	int z = -1, m = (getenv("VCC_VMREG_CHECK") ? 1 : 0) | (getenv("VCC_VMREG_STATS") ? 2 : 0);

	if (__atomic_compare_exchange_n(&vmreg_dbg_mode, &z, m, 0, __ATOMIC_RELAXED, __ATOMIC_RELAXED) && (m & 2))
		atexit(vmreg_dbg_report);
	return m;
}

void vmreg_dbg_put(vmreg *r, size_t used)
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
		if (used == VMREG_SCRATCH) {
			if (vmreg_dbg_mode & 1)
				memset(pp, 0xa5, pg);
		} else if ((vmreg_dbg_mode & 1) && k * pg + pg > used) {
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
	pthread_mutex_lock(&vmreg_dbg_mu);
	for (i = 0; i < 63 && vmreg_dbg_tab[i].name && strcmp(vmreg_dbg_tab[i].name, r->name); i++)
		;
	vmreg_dbg_tab[i].name = r->name;
	vmreg_dbg_tab[i].res += res;
	vmreg_dbg_tab[i].puts++;
	vmreg_dbg_tab[i].bad += bad;
	pthread_mutex_unlock(&vmreg_dbg_mu);
	if (bad && (vmreg_dbg_mode & 1))
		abort();
}

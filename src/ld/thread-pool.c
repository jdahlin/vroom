/* pfor(), the parallel loop the linker's passes run on: the process' shared
   pool (port/task.c) with the small-items rule, at most nthreads wide. */

#include "port/task.h"

typedef void (*pfn)(void *arg, size_t i);
static int nthreads = 1;	/* this link's width: --threads, else the P cores */

/* fn(arg, i) for i in [0, n), dynamically scheduled; a call whose items are
   all tiny (a link of four small objects) stays on the caller */
static void pfor(size_t n, pfn fn, void *arg)
{
	vtask_parallel_for(n, nthreads, fn, arg, VTASK_SMALL_ITEMS);
}

/* the width of a link without --threads */
static int pfor_default_width(void)
{
	int n = vtask_cpus_probe().performance;

	return n > 64 ? 64 : n;
}

/*
 * Apple PMU counters of the calling thread (pmc.c): pmc_init(events) opens
 * cycles, instructions and the named events (root only, else it returns 0);
 * pmc_read fills one value per counter, named pmc_names[0 .. pmc_n).  Used by
 * lextest for PMC runs; instructions and cycles alone are
 * performance-counters.h and need no root.
 */
#ifndef VCC_PORT_DARWIN_PMC_H
#define VCC_PORT_DARWIN_PMC_H
#include <stdint.h>

enum { PMC_N = 10 };
extern const char *pmc_names[PMC_N];
extern int pmc_n;
/* 0 if counting (root only); events: NULL or "EV1,EV2,..." besides cycles and instructions */
int pmc_init(const char *events);
void pmc_read(uint64_t v[PMC_N]);	/* this thread's counters */

#endif

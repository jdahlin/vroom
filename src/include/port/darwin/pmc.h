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

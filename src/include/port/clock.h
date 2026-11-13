/*
 * Wall-clock time for the tools' own timing reports (--bench, --stats,
 * the scheduler's phases): vclock_seconds() is a monotonic clock in seconds,
 * unrelated to the date.  Instruction and cycle counts are
 * performance-counters.h.
 */
#ifndef VCC_PORT_CLOCK_H
#define VCC_PORT_CLOCK_H

#include <time.h>

static inline double vclock_seconds(void)
{
	struct timespec t;

	clock_gettime(CLOCK_MONOTONIC, &t);
	return (double)t.tv_sec + (double)t.tv_nsec * 1e-9;
}

#endif

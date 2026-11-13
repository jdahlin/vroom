/*
 * vperf: instructions and cycles retired by the calling thread, without
 * root, through XNU's thread_selfcounts (the counters proc_pid_rusage's
 * ri_instructions/ri_cycles sum up).  A read costs ~1 600 instructions /
 * ~500 cycles, so use it per job or per phase, never per token.
 *
 * Instruction counts barely depend on machine load (~0.5 % noise from
 * interrupts), which is what bench/vbench.py compares; cycles do.
 */
#ifndef VCC_PORT_PERFORMANCE_COUNTERS_H
#define VCC_PORT_PERFORMANCE_COUNTERS_H

#include <stdint.h>

typedef struct vperf {
	uint64_t ins, cyc;
} vperf;

#ifdef __APPLE__
#include <sys/syscall.h>
#include <unistd.h>

static inline vperf vperformance_now(void)
{
	uint64_t c[2] = { 0, 0 };

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
	syscall(SYS_thread_selfcounts, 1, c, sizeof(c));	/* 1: THSC_CPI, {instructions, cycles} */
#pragma clang diagnostic pop
	return (vperf){ c[0], c[1] };
}
#else
static inline vperf vperformance_now(void)
{
	return (vperf){ 0, 0 };
}
#endif

static inline vperf vperformance_subtract(vperf a, vperf b)
{
	return (vperf){ a.ins - b.ins, a.cyc - b.cyc };
}

#endif

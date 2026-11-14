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
#elif defined(__linux__)
#include <linux/perf_event.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

/* Linux: the thread's user-space instructions and cycles, one counter group per
 * thread opened on first use (-1 when perf events are not allowed: zeros, as before) */
static inline vperf vperformance_now(void)
{
	static __thread int fd = -2;

	if (fd == -2) {
		struct perf_event_attr a;
		int lead;

		memset(&a, 0, sizeof a);
		a.size = sizeof a;
		a.type = PERF_TYPE_HARDWARE;
		a.config = PERF_COUNT_HW_INSTRUCTIONS;
		a.exclude_kernel = a.exclude_hv = 1;
		a.read_format = PERF_FORMAT_GROUP;
		lead = (int)syscall(SYS_perf_event_open, &a, 0, -1, -1, 0);
		if (lead >= 0) {
			a.config = PERF_COUNT_HW_CPU_CYCLES;
			if (syscall(SYS_perf_event_open, &a, 0, -1, lead, 0) < 0) {
				close(lead);
				lead = -1;
			}
		}
		fd = lead;
	}
	uint64_t v[3] = { 0, 0, 0 };	/* nr, instructions, cycles */

	if (fd < 0 || read(fd, v, sizeof v) != (ssize_t)sizeof v)
		return (vperf){ 0, 0 };
	return (vperf){ v[1], v[2] };
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

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
#include <pthread.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

/* Linux: one counter group per calling thread.  Keep both descriptors until
 * thread exit; closing only the leader does not close the group's members. */
typedef struct vperformance_linux_group {
	int leader, cycles;
} vperformance_linux_group;

static pthread_key_t vperformance_linux_key;
static pthread_once_t vperformance_linux_once = PTHREAD_ONCE_INIT;
static int vperformance_linux_key_error;

static void vperformance_linux_close(void *p)
{
	vperformance_linux_group *g = p;

	if (g->cycles >= 0)
		close(g->cycles);
	if (g->leader >= 0)
		close(g->leader);
}

static void vperformance_linux_key_init(void)
{
	vperformance_linux_key_error = pthread_key_create(&vperformance_linux_key, vperformance_linux_close);
}

static inline vperf vperformance_now(void)
{
	static __thread vperformance_linux_group g = { -2, -1 };

	if (g.leader == -2) {
		struct perf_event_attr a;

		g.leader = -1;
		pthread_once(&vperformance_linux_once, vperformance_linux_key_init);
		if (vperformance_linux_key_error || pthread_setspecific(vperformance_linux_key, &g))
			return (vperf){ 0, 0 };
		memset(&a, 0, sizeof a);
		a.size = sizeof a;
		a.type = PERF_TYPE_HARDWARE;
		a.config = PERF_COUNT_HW_INSTRUCTIONS;
		a.exclude_kernel = a.exclude_hv = 1;
		a.read_format = PERF_FORMAT_GROUP;
		g.leader = (int)syscall(SYS_perf_event_open, &a, 0, -1, -1, PERF_FLAG_FD_CLOEXEC);
		if (g.leader >= 0) {
			a.config = PERF_COUNT_HW_CPU_CYCLES;
			g.cycles = (int)syscall(SYS_perf_event_open, &a, 0, -1, g.leader, PERF_FLAG_FD_CLOEXEC);
			if (g.cycles < 0) {
				close(g.leader);
				g.leader = -1;
			}
		}
	}
	uint64_t v[3] = { 0, 0, 0 }; /* nr, instructions, cycles */

	if (g.leader < 0 || read(g.leader, v, sizeof v) != (ssize_t)sizeof v)
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

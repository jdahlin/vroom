/*
 * vtask: the one thread pool of the process, and the budget of CPU threads
 * that every part of vcc draws from (doc/quality/WORK-scheduler.md).
 *
 * The budget is the number of threads that may run CPU work at once:
 * pool threads inside an item or a job, plus outside threads (main, vccd's
 * old workers) while they run a vtask_parallel_for.  It defaults to the
 * cores the process is granted (vtask_cpus_probe) and is set by a tool's -j
 * or VCC_JOBS.  Nested work only ever borrows idle pool threads while the
 * count is below the budget: there is never a pool inside a pool, and on a
 * full machine a nested loop runs on its caller alone.
 *
 * Threads are created lazily (up to the budget plus the threads inside a
 * blocking region), all with one stack size, and live as long as the
 * process.  Idle threads sleep (no polling) and are woken last-in first-out,
 * so the few that are busy stay warm.  A pooled thread keeps its
 * thread-locals from one job to the next: whatever a job sets in a
 * thread-local, the job clears (the pool runs one job or item at a time on
 * a thread, to its end).  Helpers of a loop never carry a job's context:
 * an item run by a helper must not vjob_fail.
 *
 * Every function here is thread safe.
 */
#ifndef VCC_PORT_TASK_H
#define VCC_PORT_TASK_H

#include <stddef.h>

typedef struct vtask_cpus {
	int performance;	/* P cores: macOS hw.perflevel0; elsewhere = granted */
	int efficiency;		/* E cores: macOS hw.perflevel1; elsewhere 0 */
	int granted;		/* cores the process may use: affinity, cgroup quota, online */
} vtask_cpus;

/* the machine, probed once (all fields >= 0, granted >= 1) */
vtask_cpus vtask_cpus_probe(void);

/* threads that may run CPU work at once (>= 1) */
int vtask_budget(void);
/* any time; n <= 0: the default (granted cores).  Lowering it lets running
   work finish; no new work starts until the count is below the new budget. */
void vtask_budget_set(int n);
/* VCC_JOBS from the environment if it is a positive number, else fallback */
int vtask_jobs_from_environment(int fallback);
/* the stack of every pool thread; before the first one starts (later calls
   are ignored).  0 (the default): as large as the main thread's (its rlimit),
   so that input nests as deep on a pool thread as in a one-thread tool. */
void vtask_stack_size_set(size_t bytes);

typedef void vtask_item_fn(void *arg, size_t index);
typedef void vtask_fn(void *arg);

enum {
	/* most items are tiny: the caller runs items alone for 30 us, then wakes
	   only as many helpers as the measured cost of the rest keeps busy for
	   60 us each (a link of four small objects never wakes one) */
	VTASK_SMALL_ITEMS = 1,
};

/* fn(arg, i) exactly once for each i in [0, n), on the caller and at most
   width - 1 helpers (width <= 0: the budget); returns when every call has.
   Items are handed out in increasing order by one atomic index.  The caller
   takes every item no helper took, so the loop finishes without any pool
   thread, and an item may itself run a vtask_parallel_for. */
void vtask_parallel_for(size_t n, int width, vtask_item_fn *fn, void *arg, unsigned flags);

/* a job: queued (FIFO) and run on a pool thread when the budget allows */
void vtask_spawn(vtask_fn *fn, void *arg);
/* until no spawned job is queued or running */
void vtask_wait_idle(void);

/* around a call that waits without using the CPU (a socket, a child, a
   lock held by a long job): the thread's budget slot goes to queued work
   meanwhile.  Coming back may put the count over the budget for a moment;
   no new work starts until it is below again.  No effect on a thread the
   budget does not count.  Pairs nest. */
void vtask_blocking_begin(void);
void vtask_blocking_end(void);

/* 0..n-1 on a pool thread (stable for the thread's life), -1 elsewhere */
int vtask_worker_index(void);

/* counters for tests and measurements */
typedef struct vtask_stats {
	int threads;		/* pool threads created */
	int running;		/* threads counted against the budget now */
	int blocked;		/* counted threads inside a blocking region */
	int idle;		/* pool threads asleep */
	unsigned long wakes;	/* idle threads woken */
	unsigned long jobs;	/* jobs spawned */
} vtask_stats;
vtask_stats vtask_stats_get(void);

#endif
